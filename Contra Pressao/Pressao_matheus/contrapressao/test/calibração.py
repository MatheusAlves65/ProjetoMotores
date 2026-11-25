"""
Identificação do modelo da válvula de escape (motor DC de 2 fios + BTS7960)
a partir do log do código de calibração.

Modelo (tudo referido ao eixo da borboleta):
    V = R*i + Ke*w
    J*dw/dt = Kt*i - T_atrito*sinal(w) - b*w        (Kt = Ke no SI)
    dtheta/dt = w,  com 0 <= theta <= theta_max (batentes)

Uso:
    python modelo_valvula.py --log logs/device-monitor.log --vfonte 12.3
    python modelo_valvula.py --log meu_log.txt --vfonte 12.3 --theta 90 --r-multimetro 2.4

Requer: numpy, scipy, matplotlib  (pip install numpy scipy matplotlib)
"""

import argparse
import json
import re
import sys

import numpy as np
from scipy.optimize import curve_fit
import matplotlib.pyplot as plt

# Mesmos parâmetros de detecção do firmware de calibração
T_BLANK = 0.040        # s
T_PATAMAR = 0.060      # s, trecho final usado para medir o patamar do batente
FRACAO_BATENTE = 0.85  # batente = corrente acima de 85% do patamar final
T_ASSENTA = 0.020      # s, ignorado logo após o batente ao medir a corrente de batente


# ---------------------------------------------------------------- leitura
def ler_log(caminho):
    """Lê todas as corridas (curva CSV + resumo) do log do Monitor Serial."""
    with open(caminho, encoding="utf-8", errors="ignore") as f:
        linhas = [l.strip() for l in f]

    corridas = []
    i = 0
    while i < len(linhas):
        if linhas[i].startswith("--- CURVA (CSV) ---"):
            t, cor = [], []
            i += 1
            while i < len(linhas) and not linhas[i].startswith("--- FIM DA CURVA"):
                m = re.match(r"^(\d+),(-?[\d.]+)$", linhas[i])
                if m:
                    t.append(int(m.group(1)) / 1000.0)
                    cor.append(float(m.group(2)))
                i += 1
            sentido, pwm = None, None
            j = i
            while j < len(linhas) and j < i + 15:
                ms = re.match(r"^Sentido:\s+(\w+)", linhas[j])
                mp = re.match(r"^PWM:\s+(\d+)/255", linhas[j])
                if ms:
                    sentido = ms.group(1).upper()
                if mp:
                    pwm = int(mp.group(1))
                j += 1
            if sentido and pwm and len(t) > 10:
                corridas.append({"sentido": sentido, "pwm": pwm,
                                 "t": np.array(t), "i_lida": np.array(cor)})
        i += 1
    return corridas


# ---------------------------------------------------------------- análise
def detectar_batente(t, i_lida):
    """Repete a lógica do firmware: o batente é o instante a partir do qual a
    corrente (média de 2 amostras) fica acima de 85% do patamar final.
    Retorna o tempo do batente (s) ou None."""
    final = t >= t[-1] - T_PATAMAR + 1e-9
    if final.sum() < 3:
        return None
    patamar = float(np.mean(i_lida[final]))
    limiar = FRACAO_BATENTE * patamar
    k_bat = None
    for k in range(len(t) - 1, 0, -1):
        if t[k] < T_BLANK:
            break
        if 0.5 * (i_lida[k] + i_lida[k - 1]) < limiar:
            k_bat = k + 1
            break
    if k_bat is None or t[k_bat] >= t[-1] - T_PATAMAR:
        return None          # não chegou ao batente dentro da corrida
    return float(t[k_bat])


def exp_decay(t, a, b, tau):
    return a + b * np.exp(-t / tau)


def analisar_corrida(c, vfonte, fator_corrente):
    D = c["pwm"] / 255.0
    c["D"] = D
    c["V"] = D * vfonte
    # corrente real do motor: IS mede a média do tempo ligado
    c["i"] = c["i_lida"] / D * fator_corrente

    t, i = c["t"], c["i"]
    tb = detectar_batente(c["t"], c["i_lida"])
    c["t_bat"] = tb
    if tb is None:
        return c

    fim_giro = tb - 0.02
    ini_giro = T_BLANK if tb > T_BLANK + 0.05 else 0.5 * tb
    giro = (t >= ini_giro) & (t <= fim_giro)
    bat = t >= tb + T_ASSENTA
    c["i_gir"] = float(np.median(i[giro])) if giro.any() else np.nan
    c["i_bat"] = float(np.median(i[bat])) if bat.any() else np.nan

    # constante de tempo mecânica: queda da corrente após a partida
    sel = (t > 0) & (t <= fim_giro)
    c["tau"] = np.nan
    if sel.sum() >= 5:
        tt, ii = t[sel], i[sel]
        try:
            p0 = [c["i_gir"], max(ii[0] - c["i_gir"], 0.1), 0.03]
            popt, _ = curve_fit(exp_decay, tt, ii, p0=p0,
                                bounds=([0, 0, 0.001], [np.inf, np.inf, 2.0]),
                                maxfev=10000)
            c["tau"] = float(popt[2])
            if popt[2] < 0.8 * tb:
                c["i_gir"] = float(popt[0])   # valor de regime, sem a cauda da partida
        except Exception:
            pass
    return c


def identificar(corridas, theta_max, R):
    """Ajusta Ke, T_atrito, b e J para um sentido."""
    validas = [c for c in corridas if c["t_bat"] is not None and not np.isnan(c["i_gir"])]
    if not validas:
        return None

    w, e, igir, taus = [], [], [], []
    for c in validas:
        tau = c["tau"] if not np.isnan(c["tau"]) and c["tau"] < 0.8 * c["t_bat"] else 0.0
        tb = c["t_bat"]
        # theta(tb) = w_ss * (tb - tau*(1 - e^(-tb/tau)))  -> velocidade em regime
        atraso = tau * (1 - np.exp(-tb / tau)) if tau > 0 else 0.0
        w_ss = theta_max / (tb - atraso)
        c["w_ss"] = w_ss
        w.append(w_ss)
        e.append(c["V"] - R * c["i_gir"])          # força contraeletromotriz
        igir.append(c["i_gir"])
        if tau > 0:
            taus.append(tau)
    w, e, igir = np.array(w), np.array(e), np.array(igir)

    Ke = float(np.sum(e * w) / np.sum(w * w))      # reta pela origem: e = Ke*w
    Kt = Ke

    if len(validas) >= 2 and np.ptp(w) > 0.05 * np.mean(w):
        c1, c0 = np.polyfit(w, igir, 1)            # i_gir = c0 + c1*w
        c0, c1 = max(c0, 0.0), max(c1, 0.0)
    else:
        c0, c1 = float(np.mean(igir)), 0.0
    T_at = Kt * c0
    b = Kt * c1

    tau_m = float(np.median(taus)) if taus else np.nan
    J = tau_m * (b + Kt * Ke / R) if not np.isnan(tau_m) else np.nan

    return {"Ke": Ke, "Kt": Kt, "T_atrito": T_at, "b": b, "J": J,
            "tau_m": tau_m, "V_minima": R * c0, "n_corridas": len(validas)}


# ---------------------------------------------------------------- simulação
def simular(V, p, R, theta_max, t_fim, dt=0.0005):
    n = int(t_fim / dt) + 1
    t = np.arange(n) * dt
    w = theta = 0.0
    i_out, th_out = np.zeros(n), np.zeros(n)
    J = p["J"] if not np.isnan(p["J"]) else 1e-6
    for k in range(n):
        i = (V - p["Ke"] * w) / R
        T_mot = p["Kt"] * i
        if theta >= theta_max:
            theta, w = theta_max, 0.0
            i = V / R
        elif w <= 0.0 and T_mot <= p["T_atrito"]:
            w = 0.0                                 # atrito estático segura
        else:
            dw = (T_mot - p["T_atrito"] - p["b"] * w) / J
            w = max(w + dw * dt, 0.0)
            theta += w * dt
        i_out[k], th_out[k] = i, theta
    return t, i_out, th_out


# ---------------------------------------------------------------- principal
def main():
    ap = argparse.ArgumentParser(description="Identifica o modelo da válvula.")
    ap.add_argument("--log", required=True, help="arquivo de log do Monitor Serial")
    ap.add_argument("--vfonte", type=float, required=True,
                    help="tensão da fonte medida com multímetro durante o teste (V)")
    ap.add_argument("--theta", type=float, default=90.0,
                    help="curso da borboleta em graus (padrão 90)")
    ap.add_argument("--r-multimetro", type=float, default=None,
                    help="resistência medida no multímetro (ohm), para corrigir a escala do IS")
    ap.add_argument("--saida", default="modelo_valvula",
                    help="prefixo dos arquivos de saída (.json e .png)")
    args = ap.parse_args()

    theta_max = np.deg2rad(args.theta)
    corridas = ler_log(args.log)
    if not corridas:
        sys.exit("Nenhuma corrida encontrada no log.")
    print(f"{len(corridas)} corridas encontradas.\n")

    # 1ª passada sem correção para obter a corrente de batente
    for c in corridas:
        analisar_corrida(c, args.vfonte, 1.0)
    com_bat = [c for c in corridas if c["t_bat"] is not None and not np.isnan(c.get("i_bat", np.nan))]
    if not com_bat:
        sys.exit("Nenhuma corrida chegou ao batente. Aumente T_ACIONA_MS no firmware.")

    fator = 1.0
    if args.r_multimetro:
        # corrente esperada no batente = V / R_multímetro
        razoes = [(c["V"] / args.r_multimetro) / c["i_bat"] for c in com_bat]
        fator = float(np.median(razoes))
        print(f"Correção da escala do IS: x{fator:.3f}")
        print(f"  -> no firmware, use RAZAO_IS = {8500 * fator:.0f}\n")
        for c in corridas:
            analisar_corrida(c, args.vfonte, fator)

    R_runs = [c["V"] / c["i_bat"] for c in corridas
              if c["t_bat"] is not None and not np.isnan(c.get("i_bat", np.nan))]
    R = args.r_multimetro if args.r_multimetro else float(np.median(R_runs))

    # tabela das corridas
    print(f"{'Sentido':8} {'PWM':>4} {'V(V)':>6} {'curso(ms)':>10} "
          f"{'i_gir(A)':>9} {'i_bat(A)':>9} {'tau(ms)':>8}")
    for c in corridas:
        if c["t_bat"] is None:
            print(f"{c['sentido']:8} {c['pwm']:>4} {c['V']:>6.2f}   batente não detectado")
            continue
        print(f"{c['sentido']:8} {c['pwm']:>4} {c['V']:>6.2f} {c['t_bat']*1000:>10.0f} "
              f"{c['i_gir']:>9.2f} {c['i_bat']:>9.2f} {c['tau']*1000:>8.1f}")

    print(f"\nResistência R = {R:.3f} ohm "
          f"({'multímetro' if args.r_multimetro else 'corrente de batente'})")

    resultado = {"R": R, "theta_max_rad": theta_max, "vfonte": args.vfonte,
                 "fator_IS": fator, "sentidos": {}}

    for sentido in sorted({c["sentido"] for c in corridas}):
        grupo = [c for c in corridas if c["sentido"] == sentido]
        p = identificar(grupo, theta_max, R)
        if p is None:
            print(f"\n{sentido}: sem dados suficientes.")
            continue
        resultado["sentidos"][sentido] = p
        print(f"\n=== {sentido} ({p['n_corridas']} corridas) ===")
        print(f"  Ke' = Kt'   = {p['Ke']:.4f} V/(rad/s)  =  N.m/A")
        print(f"  T_atrito    = {p['T_atrito']:.4f} N.m")
        print(f"  b           = {p['b']:.5f} N.m/(rad/s)")
        print(f"  J'          = {p['J']:.3e} kg.m^2")
        print(f"  tau_m       = {p['tau_m']*1000:.1f} ms")
        print(f"  V mínima p/ mover = {p['V_minima']:.2f} V "
              f"(PWM ~{255 * p['V_minima'] / args.vfonte:.0f}/255)")
        if not np.isnan(p["tau_m"]):
            k = 1 / (p["Ke"] + p["b"] * R / p["Kt"])
            print(f"  Função de transferência (sem atrito constante):")
            print(f"     theta(s)/V(s) = {k:.3f} / [ s * ({p['tau_m']:.4f}*s + 1) ]")
        if not np.isnan(p["tau_m"]) and p["tau_m"] < 0.010:
            print("  ATENÇÃO: tau_m < 10 ms, perto da amostragem. "
                  "Reduza INTERVALO_MS no firmware para melhorar.")

    with open(args.saida + ".json", "w") as f:
        json.dump(resultado, f, indent=2)
    print(f"\nParâmetros salvos em {args.saida}.json")

    # gráficos: medido x simulado
    validas = [c for c in corridas if c["t_bat"] is not None and c["sentido"] in resultado["sentidos"]]
    if not validas:
        return
    fig, eixos = plt.subplots(len(validas), 2, figsize=(11, 2.8 * len(validas)), squeeze=False)
    for k, c in enumerate(validas):
        p = resultado["sentidos"][c["sentido"]]
        ts, isim, thsim = simular(c["V"], p, R, theta_max, c["t"][-1])
        idx = np.argmax(thsim >= theta_max)
        t_sim_bat = ts[idx] if thsim[-1] >= theta_max else np.nan

        ax = eixos[k][0]
        ax.plot(c["t"] * 1000, c["i"], ".", ms=3, label="medido")
        ax.plot(ts * 1000, isim, "-", label="modelo")
        ax.axvline(c["t_bat"] * 1000, color="gray", ls="--", lw=0.8)
        ax.set_title(f"{c['sentido']} PWM {c['pwm']}  |  curso medido {c['t_bat']*1000:.0f} ms, "
                     f"modelo {t_sim_bat*1000:.0f} ms", fontsize=9)
        ax.set_xlabel("t (ms)"); ax.set_ylabel("corrente (A)"); ax.legend(fontsize=8)
        ax.grid(alpha=0.3)

        ax = eixos[k][1]
        ax.plot(ts * 1000, np.rad2deg(thsim))
        ax.set_xlabel("t (ms)"); ax.set_ylabel("ângulo modelo (°)")
        ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(args.saida + ".png", dpi=120)
    print(f"Gráficos salvos em {args.saida}.png")
    plt.show()


if __name__ == "__main__":
    main()