"""
Identificação do atuador (motor DC + redutor + borboleta) por ensaios separados,
no estilo de Loh et al., "Electronic Throttle Control System: Modeling,
Identification and Model-Based Control Designs", Engineering 5 (2013) 587-600,
doi:10.4236/eng.2013.57071.

Modelo (referido ao eixo da borboleta, indutância desprezada):
    V = R·i + Ke·ω
    J·dω/dt = Kt·i - Tf·sgn(ω) - B·ω        (Kt = Ke no SI)

Comandos
  rampa        linhas "R," do comando 'rampa <1|-1>' do firmware  (seções 3.1 e 3.4)
               - parada: i = V/R (reta). Confere R e a linearidade do IS.
               - descolamento: onde a corrente sai da reta -> torque de atrito Tf.
               Com os dois sentidos, separa atrito de carga constante
               (mola/peso), como na Figura 8 do artigo.

  velocidades  linhas "P," do comando 'pwm <v> <ms>' com cursos completos  (seção 3.5)
               - cada PWM dá uma velocidade ω = curso/tempo e uma corrente de giro i
               - Ke: reta e = V - R·i = Ke·ω
               - Tf e B: reta T = Kt·i = Tf + B·ω

Uso:
  python test/atuador.py rampa       logs/rampas.log
  python test/atuador.py velocidades logs/velocidades.log
Opções: --r (ohm), --kt (N·m/A), --modelo modelo_valvula.json, --theta (graus)

Requer: numpy, matplotlib
"""

import argparse
import json
import os
import re
import sys

import numpy as np

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

COR_DADOS = "#4a4a4a"
COR_MODELO = "#2a6fdb"
COR_DESTAQUE = "#d1495b"

RE_RAMPA = re.compile(r"# RAMPA (FECHAR|ABRIR) (concluida|INTERROMPIDA|LIMITE): (\d+) amostras "
                      r"\| Vbat ([\d.]+) V \| k_IS (\d+) mV/A \| R_MOTOR ([\d.]+)")
NOME = {1: "FECHAR", -1: "ABRIR"}


def carregar_modelo(caminho):
    if caminho and os.path.exists(caminho):
        with open(caminho, encoding="utf-8") as f:
            return json.load(f)
    return None


def kt_do(args, modelo, sentido):
    if args.kt:
        return args.kt
    if modelo and NOME[sentido] in modelo.get("sentidos", {}):
        return modelo["sentidos"][NOME[sentido]]["Kt"]
    return None


def r_do(args, modelo):
    if args.r:
        return args.r
    if modelo and "R" in modelo:
        return modelo["R"]
    return 4.43


def estilo(a):
    a.grid(alpha=0.25)
    a.spines[["top", "right"]].set_visible(False)


# ---------------------------------------------------------------- leitura
def ler(caminho):
    rampas, pontos_p = [], []
    atual = None
    with open(caminho, encoding="utf-8", errors="ignore") as f:
        for linha in f:
            s = linha.strip()
            m = RE_RAMPA.match(s)
            if m:
                atual = {"sentido": 1 if m.group(1) == "FECHAR" else -1,
                         "interrompida": m.group(2) == "INTERROMPIDA",
                         "vbat": float(m.group(4)), "k_is": float(m.group(5)),
                         "r_fw": float(m.group(6)), "V": [], "i": [], "mv": [], "pwm": []}
                rampas.append(atual)
                continue
            if s.startswith("R,") and atual is not None:
                c = s.split(",")
                if len(c) == 7 and c[1] in ("1", "-1"):
                    atual["pwm"].append(float(c[3]))
                    atual["V"].append(float(c[4]))
                    atual["mv"].append(float(c[5]))
                    atual["i"].append(float(c[6]))
                continue
            if s.startswith("P,"):
                c = s.split(",")
                if len(c) == 7 and c[1].lstrip("-").isdigit():
                    pontos_p.append({"pwm": int(c[1]), "V": float(c[2]),
                                     "parada_ms": float(c[3]), "motivo": c[4],
                                     "i": float(c[5]) if c[5] not in ("nan", "") else np.nan})
    for r in rampas:
        for k in ("V", "i", "mv", "pwm"):
            r[k] = np.array(r[k])
    return rampas, pontos_p


# ---------------------------------------------------------------- rampa
def reta_origem(V, i):
    """i = V/R pela origem, mínimos quadrados. Retorna (R, R²)."""
    g = np.sum(V * i) / np.sum(V * V)                  # condutância
    ih = g * V
    r2 = 1 - np.sum((i - ih) ** 2) / max(np.sum((i - i.mean()) ** 2), 1e-12)
    return 1.0 / g, r2


def analisar_rampa(rp, v_min, min_seg, inclinacao_parado):
    """Ajuste em dois segmentos:
         parado:   i = V/R            (reta pela origem)
         girando:  i = a·V + b        (corrente quase constante = atrito)
       O descolamento é o cruzamento das duas retas. Se a reta depois do joelho
       ainda tiver inclinação próxima de 1/R, a válvula não descolou (batente)."""
    V, i = rp["V"], rp["i"]
    idx = np.where(V >= v_min)[0]                      # abaixo disso o IS é ruído
    if len(idx) < 2 * min_seg:
        return None
    Vv, iv = V[idx], i[idx]
    R_todo, r2_todo = reta_origem(Vv, iv)
    melhor = None
    for kb in range(min_seg, len(idx) - min_seg):
        Rp, _ = reta_origem(Vv[:kb], iv[:kb])
        a, b = np.polyfit(Vv[kb:], iv[kb:], 1)
        sse = np.sum((iv[:kb] - Vv[:kb] / Rp) ** 2) + np.sum((iv[kb:] - (a * Vv[kb:] + b)) ** 2)
        if melhor is None or sse < melhor[0]:
            melhor = (sse, kb, Rp, a, b)
    res = {"sentido": rp["sentido"], "interrompida": rp["interrompida"],
           "R": R_todo, "R2": r2_todo, "n_parado": int(len(idx))}
    if melhor is None:
        return res
    _, kb, Rp, a, b = melhor
    if a > inclinacao_parado / Rp:                     # continuou subindo como parado
        return res
    Vd = b / (1.0 / Rp - a)                            # cruzamento das retas
    parado = Vv < Vd
    if parado.sum() >= 5:
        Rp, r2p = reta_origem(Vv[parado], iv[parado])
    else:
        r2p = np.nan
    res.update({"R": Rp, "R2": r2p, "n_parado": int(parado.sum()),
                "V_desc": float(Vd), "i_desc": float(Vd / Rp),
                "i_giro_A": float(a * Vv[-1] + b), "inclinacao_giro": float(a)})
    return res


def cmd_rampa(args):
    modelo = carregar_modelo(args.modelo)
    rampas, _ = ler(args.log)
    if not rampas:
        sys.exit("Nenhuma rampa no log. No firmware: 'rampa 1' (fecha) ou 'rampa -1' (abre).")

    resultados = []
    print(f"{len(rampas)} rampa(s) encontrada(s).\n")
    print(f"{'#':>2} {'sentido':>7} | {'R ohm':>6} {'R² reta':>7} | {'V desc V':>8} "
          f"{'i desc A':>8} {'Tf N·m':>7} | situação")
    for n, rp in enumerate(rampas):
        res = analisar_rampa(rp, args.v_min, args.min_seg, args.inclinacao_parado)
        if res is None:
            print(f"{n:2d} {NOME[rp['sentido']]:>7} | poucos pontos acima de {args.v_min} V")
            continue
        kt = kt_do(args, modelo, rp["sentido"])
        if "V_desc" in res and kt:
            res["Tf"] = kt * res["i_desc"]
        situ = ("parada o tempo todo (batente): só R e linearidade do IS"
                if "V_desc" not in res else "descolou")
        if res["interrompida"]:
            situ += " [INTERROMPIDA]"
        print(f"{n:2d} {NOME[rp['sentido']]:>7} | {res['R']:6.2f} {res['R2']:7.3f} | "
              f"{res.get('V_desc', np.nan):8.2f} {res.get('i_desc', np.nan):8.3f} "
              f"{res.get('Tf', np.nan):7.3f} | {situ}")
        resultados.append((rp, res))

    print("\nObs.: o R desta reta NÃO é independente do multímetro: o k_IS da autocalibração")
    print("      foi calculado com R_MOTOR. O que ela confere é a LINEARIDADE (R² perto de 1")
    print("      e R constante em toda a rampa) do modelo do IS, mV = k·D·i, em vários duty cycles.")

    # separação atrito x carga constante (Figura 8 de Loh et al.)
    tf = {s: [r["Tf"] for _, r in resultados if r["sentido"] == s and "Tf" in r] for s in (1, -1)}
    if tf[1] and tf[-1]:
        tF, tA = np.mean(tf[1]), np.mean(tf[-1])
        print(f"\nAtrito médio de descolamento: fechar {tF:.3f} N·m, abrir {tA:.3f} N·m")
        print(f"  atrito (média)              Tf    = {(tF + tA) / 2:.3f} N·m")
        print(f"  carga constante (meia-dif.) Tcarga = {(tF - tA) / 2:+.3f} N·m "
              "(+ = resiste ao fechamento: mola, peso ou assentamento)")
        if modelo:
            vm = {s: modelo["sentidos"][s]["V_minima"] for s in modelo.get("sentidos", {})}
            print(f"  comparação: V_minima do modelo_valvula.json = {vm}")
    elif not any("Tf" in r for _, r in resultados):
        print("\nNenhum descolamento medido. Para medir o atrito, comece com a válvula no MEIO "
              "do curso ('m 50') antes da rampa.")

    base = os.path.splitext(args.saida or args.log)[0] + "_rampa"
    with open(base + ".json", "w", encoding="utf-8") as f:
        json.dump([{k: (None if isinstance(v, float) and np.isnan(v) else v)
                    for k, v in r.items()} for _, r in resultados], f, indent=2, default=float)

    n = len(resultados)
    if n:
        ncol = min(2, n)
        nlin = int(np.ceil(n / ncol))
        fig, axs = plt.subplots(nlin, ncol, figsize=(5.5 * ncol, 3.8 * nlin), squeeze=False)
        for k, (rp, res) in enumerate(resultados):
            a = axs[k // ncol][k % ncol]
            a.plot(rp["V"], rp["i"], ".", ms=3, color=COR_DADOS, label="medido (IS)")
            vv = np.linspace(0, rp["V"].max(), 50)
            a.plot(vv, vv / res["R"], lw=2, color=COR_MODELO,
                   label=f"parado: i = V/R, R = {res['R']:.2f} Ω")
            if "V_desc" in res:
                a.plot(res["V_desc"], res["i_desc"], "o", ms=9, mfc="none", mew=2,
                       color=COR_DESTAQUE, label=f"descolamento: {res['V_desc']:.2f} V")
            a.axvline(args.v_min, color=COR_DADOS, lw=0.8, ls=":")
            a.set_title(f"Rampa {NOME[rp['sentido']]} – "
                        f"{'descolou' if 'V_desc' in res else 'parada (batente)'}", fontsize=10)
            a.set_xlabel("tensão média no motor D·Vbat (V)")
            a.set_ylabel("corrente (A)")
            a.legend(frameon=False, fontsize=8)
            estilo(a)
        for k in range(n, nlin * ncol):
            axs[k // ncol][k % ncol].axis("off")
        fig.tight_layout()
        fig.savefig(base + ".png", dpi=150)
    print(f"\nSalvo: {base}.json e {base}.png")


# ---------------------------------------------------------------- velocidades
def cmd_velocidades(args):
    modelo = carregar_modelo(args.modelo)
    _, pts = ler(args.log)
    R = r_do(args, modelo)
    theta = np.deg2rad(args.theta)
    validos = [p for p in pts if p["motivo"] in ("B", "M") and p["parada_ms"] > 0
               and np.isfinite(p["i"])]
    ignorados = len(pts) - len(validos)
    if ignorados:
        print(f"{ignorados} linha(s) P ignorada(s): não travaram (tempo acabou ou interrompido).")
    if not validos:
        sys.exit("Nenhum curso completo. Faça, para vários PWMs: 'm 0' e depois 'pwm <v> 5000' "
                 "(fecha tudo); 'm 100' e depois 'pwm -<v> 5000' (abre tudo).")

    print(f"R = {R:.2f} ohm | curso = {args.theta:.0f}° | "
          "ATENÇÃO: cada 'pwm' precisa começar no batente oposto (curso completo).\n")
    saida = {"R": R, "theta_max_rad": theta, "sentidos": {}}
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(11, 4.2))

    for s, marca in ((1, "o"), (-1, "s")):
        g = [p for p in validos if np.sign(p["pwm"]) == s]
        if not g:
            continue
        V = np.array([p["V"] for p in g])
        i = np.array([p["i"] for p in g])
        w = theta / (np.array([p["parada_ms"] for p in g]) / 1000.0)
        e = V - R * i                                   # força contraeletromotriz
        print(f"=== {NOME[s]} ({len(g)} cursos)")
        print(f"{'PWM':>5} {'V':>6} {'t curso ms':>10} {'ω rad/s':>8} {'i A':>6} {'e V':>6}")
        for p, wk, ek in zip(g, w, e):
            print(f"{p['pwm']:5d} {p['V']:6.2f} {p['parada_ms']:10.0f} {wk:8.3f} {p['i']:6.3f} {ek:6.2f}")

        Ke = float(np.sum(e * w) / np.sum(w * w))       # e = Ke·ω pela origem
        res = {"Ke": Ke, "Kt": Ke, "n_cursos": len(g)}
        nl = len(np.unique(np.round(w, 3)))
        if nl >= 2:
            a_, b_ = np.polyfit(w, e, 1)
            res["e_intercepto_V"] = float(b_)
            T = Ke * i
            B, Tf = np.polyfit(w, T, 1)
            res.update({"B": float(B), "Tf": float(Tf), "V_minima": float(R * Tf / Ke)})
            print(f"  Ke = Kt = {Ke:.4f} V·s/rad  (reta com intercepto: inclinação {a_:.4f}, "
                  f"intercepto {b_:+.3f} V)")
            print(f"  Tf = {Tf:.4f} N·m | B = {B:.5f} N·m·s/rad | V mínima = {res['V_minima']:.2f} V")
            if B < 0:
                print("  (B negativo: dispersão maior que o efeito viscoso; use mais PWMs/repetições)")
            a2.plot(w, T, marca, ms=7, color=COR_DADOS,
                    mfc=COR_DADOS if s > 0 else "none", label=f"{NOME[s].lower()} medido")
            ww = np.linspace(0, w.max() * 1.1, 20)
            a2.plot(ww, Tf + B * ww, lw=2, color=COR_MODELO if s > 0 else COR_DESTAQUE,
                    label=f"{NOME[s].lower()}: Tf={Tf:.2f}, B={B:.4f}")
        else:
            print(f"  Ke = Kt = {Ke:.4f} V·s/rad  (um só PWM: Tf e B não separam; use 3+ PWMs)")
        if modelo and NOME[s] in modelo.get("sentidos", {}):
            mj = modelo["sentidos"][NOME[s]]
            print(f"  modelo_valvula.json: Ke = {mj['Ke']:.4f}, Tf = {mj['T_atrito']:.4f}, "
                  f"b = {mj['b']:.5f}")
        saida["sentidos"][NOME[s]] = res
        a1.plot(w, e, marca, ms=7, color=COR_DADOS, mfc=COR_DADOS if s > 0 else "none",
                label=f"{NOME[s].lower()} medido")
        ww = np.linspace(0, w.max() * 1.1, 20)
        a1.plot(ww, Ke * ww, lw=2, color=COR_MODELO if s > 0 else COR_DESTAQUE,
                label=f"{NOME[s].lower()}: Ke={Ke:.3f}")
        print()

    a1.set_xlabel("velocidade ω (rad/s)"); a1.set_ylabel("e = V − R·i (V)")
    a1.set_title("Força contraeletromotriz", fontsize=10)
    a2.set_xlabel("velocidade ω (rad/s)"); a2.set_ylabel("torque Kt·i (N·m)")
    a2.set_title("Torque de giro: Tf + B·ω", fontsize=10)
    for a in (a1, a2):
        a.set_xlim(left=0); a.legend(frameon=False, fontsize=8); estilo(a)
    fig.tight_layout()
    base = os.path.splitext(args.saida or args.log)[0] + "_velocidades"
    fig.savefig(base + ".png", dpi=150)
    with open(base + ".json", "w", encoding="utf-8") as f:
        json.dump(saida, f, indent=2)
    print(f"Salvo: {base}.json e {base}.png")


# ---------------------------------------------------------------- main
def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for nome in ("rampa", "velocidades"):
        s = sub.add_parser(nome)
        s.add_argument("log")
        s.add_argument("--saida", help="prefixo dos arquivos de saída (padrão: o do log)")
        s.add_argument("--modelo", default="modelo_valvula.json",
                       help="JSON do calibração.py, para Kt, R e comparação")
        s.add_argument("--r", type=float, default=None, help="resistência (ohm)")
        s.add_argument("--kt", type=float, default=None, help="Kt (N·m/A), se não usar o JSON")
    g = sub.choices["rampa"]
    g.add_argument("--v-min", type=float, default=1.0,
                   help="ignora tensões abaixo disso (IS pouco confiável em duty baixo) [V]")
    g.add_argument("--min-seg", type=int, default=15,
                   help="mínimo de amostras em cada segmento do ajuste (4 ms cada)")
    g.add_argument("--inclinacao-parado", type=float, default=0.6,
                   help="se a reta após o joelho subir mais que isso × 1/R, não descolou")
    sub.choices["velocidades"].add_argument("--theta", type=float, default=90.0,
                                            help="curso da borboleta (graus)")
    args = ap.parse_args()
    {"rampa": cmd_rampa, "velocidades": cmd_velocidades}[args.cmd](args)


if __name__ == "__main__":
    main()
