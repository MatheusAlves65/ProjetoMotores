"""
Ajuste por mínimos quadrados dos ensaios de degrau em test/logs_teste_degraus.

Para cada log (um degrau 'degrau 0 80' por arquivo) ajusta, na curva inteira
após o degrau, o modelo de 1ª ordem com atraso (FOPDT):

        p(t) = p0 + Δp · (1 - e^(-(t-L)/τ)),  t >= L        G(s) = K·e^(-Ls)/(τs+1)

Depois normaliza cada resposta,  y_n = (p - p0) / Δp  (0 antes, 1 no regime),
e ajusta um único modelo normalizado  1 - e^(-(t-L)/τ)  a todos os ensaios juntos.

Saídas (na pasta dos logs):
  degraus_mq_parametros.csv     K, τ, L, R² de cada ensaio + Smith do firmware
  degraus_mq_experimentos.png   os 20 ensaios, cada um com o seu ajuste
  degraus_mq_normalizado.png    todos sobrepostos: pressão (bar) e forma normalizada

Uso:
  python test/degraus_mq.py                       (pasta padrão test/logs_teste_degraus)
  python test/degraus_mq.py outra/pasta --t-pre 2
"""

import argparse
import csv
import glob
import os
import re
import sys

import numpy as np
from scipy.optimize import curve_fit

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from identificacao import fopdt, ajustar_fopdt, COR_DADOS, COR_MODELO  # noqa: E402

import matplotlib.pyplot as plt  # noqa: E402  (Agg já escolhido em identificacao)

RE_DEGRAU = re.compile(r"# DEGRAU: ([-\d.]+) -> ([-\d.]+) % agora .*?t = (\d+) ms")
CAMPOS_D = ["de", "para", "p0", "pf", "dp", "K", "t28", "t63", "tau", "L"]


def ler(caminho):
    t, p, pos, pwm, degrau, fw = [], [], [], [], None, {}
    with open(caminho, encoding="utf-8", errors="ignore") as f:
        for s in f:
            s = s.strip()
            if s.startswith("T,"):
                c = s.split(",")[1:]
                if len(c) >= 3 and c[0].isdigit():
                    t.append(float(c[0]))
                    p.append(float(c[1]))
                    pos.append(float(c[2]))
                    pwm.append(float(c[3]) if len(c) >= 4 else np.nan)
                continue
            m = RE_DEGRAU.match(s)
            if m:
                degrau = (float(m.group(1)), float(m.group(2)), float(m.group(3)))
                continue
            if s.startswith("D,"):
                v = s.split(",")[1:]
                if len(v) == len(CAMPOS_D) and v[0] != "de_%":
                    fw = {k: float(x) for k, x in zip(CAMPOS_D, v)}
    if degrau is None:
        return None
    return {"t": (np.array(t) - degrau[2]) / 1000.0, "p": np.array(p),
            "pos": np.array(pos), "pwm": np.array(pwm),
            "de": degrau[0], "para": degrau[1], "fw": fw}


def smith(t, y):
    """Smith sobre uma resposta já normalizada (0 -> 1), igual ao firmware."""
    t28 = float(t[np.argmax(y >= 0.283)])
    t63 = float(t[np.argmax(y >= 0.632)])
    tau = 1.5 * (t63 - t28)
    return t28, t63, tau, t63 - tau


def escrever_log_normalizado(ensaios, caminho, tau_g, L_g, ts=0.02):
    """Média dos ensaios normalizados, reamostrada numa grade de `ts` s alinhada ao
    instante do degrau, no mesmo formato dos logs do firmware (T, / # DEGRAU / D,)."""
    t_ini = max(e["t"].min() for e in ensaios)
    t_fim = min(e["t"].max() for e in ensaios)
    k = np.arange(np.ceil(t_ini / ts), np.floor(t_fim / ts) + 1)
    grade = k * ts
    yn = np.mean([np.interp(grade, e["t"], e["yn"]) for e in ensaios], axis=0)
    pos = np.mean([np.interp(grade, e["t"], e["pos"]) for e in ensaios], axis=0)
    pwm = np.mean([np.interp(grade, e["t"], np.nan_to_num(e["pwm"])) for e in ensaios], axis=0)
    de, para = ensaios[0]["de"], ensaios[0]["para"]
    t0_ms = int(round(-grade[0] * 1000))               # tempo do degrau no log novo
    t_ms = np.round((grade - grade[0]) * 1000).astype(int)

    pre = grade < 0
    p0, pf = float(yn[pre].mean()), float(yn[grade >= grade[-1] - 1.0].mean())
    t28, t63, tau_s, L_s = smith(grade[~pre], yn[~pre])
    with open(caminho, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# LOG NORMALIZADO: media de {len(ensaios)} ensaios, (p - p0)/dp de cada um; "
                f"pressao adimensional (0 antes, 1 em regime); pos e pwm = media dos ensaios; "
                f"amostragem {ts*1000:.0f} ms\n")
        f.write(f"# Ensaios: {', '.join(e['nome'] for e in ensaios)}\n")
        f.write(f"# Ajuste MQ global na forma normalizada: tau {tau_g:.3f} s | L {L_g:.3f} s\n")
        f.write(f"# DEGRAU: indo para {de:.1f} % e estabilizando 5 s; depois degrau para "
                f"{para:.1f} % e registro de 5 s\n")
        for i in range(len(grade)):
            if i and grade[i - 1] < 0 <= grade[i]:
                f.write(f"# DEGRAU: {de:.1f} -> {para:.1f} % agora "
                        f"(p0 = {p0:.4f} bar, t = {t0_ms} ms)\n")
            f.write(f"T,{t_ms[i]},{yn[i]:.4f},{pos[i]:.1f},{int(round(pwm[i]))}\n")
        f.write(f"# DEGRAU resultado: {de:.1f} -> {para:.1f} %\n")
        f.write(f"#   p inicial {p0:.4f} (normalizado) | p final {pf:.4f} | delta {pf-p0:+.4f}\n")
        f.write(f"#   K = {(pf-p0)/(para-de):.5f} 1/% | t28 {t28:.2f} s | t63 {t63:.2f} s | "
                f"tau {tau_s:.3f} s | L {L_s:.3f} s  (Smith)\n")
        f.write("D,de_%,para_%,p0_bar,pf_bar,dp_bar,K_bar_por_%,t28_s,t63_s,tau_s,L_s\n")
        f.write(f"D,{de:.1f},{para:.1f},{p0:.4f},{pf:.4f},{pf-p0:.4f},{(pf-p0)/(para-de):.5f},"
                f"{t28:.3f},{t63:.3f},{tau_s:.3f},{L_s:.3f}\n")
        f.write(f"# DEGRAU terminado: concluido. Valvula em {para:.1f} %; telemetria de volta a 0 ms.\n")
    return len(grade)


def degrau_normalizado(t, tau, L):
    return fopdt(t, 0.0, 1.0, tau, L)


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    padrao = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs_teste_degraus")
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pasta", nargs="?", default=padrao)
    ap.add_argument("--t-pre", type=float, default=2.0, help="s antes do degrau para p inicial")
    args = ap.parse_args()

    def numero(a):  # 'log 07_dr.log' e 'log08_dr.log' na ordem certa
        m = re.search(r"(\d+)", os.path.basename(a))
        return int(m.group(1)) if m else 0
    arquivos = sorted((a for a in glob.glob(os.path.join(args.pasta, "*.log"))
                       if not os.path.basename(a).startswith("degraus_mq")),  # saída deste script
                      key=numero)
    ensaios = []
    for k, arq in enumerate(arquivos, start=1):
        d = ler(arq)
        if d is None:
            print(f"{os.path.basename(arq)}: sem linha '# DEGRAU ... agora', ignorado")
            continue
        t, p = d["t"], d["p"]
        pre, pos = (t >= -args.t_pre) & (t < 0), t >= 0
        y0, ruido = float(np.mean(p[pre])), float(np.std(p[pre]))
        aj = ajustar_fopdt(t[pos], p[pos], y0, ruido)
        if aj is None:
            print(f"{os.path.basename(arq)}: resposta no nível do ruído, ignorado")
            continue
        popt, r2 = aj
        d.update(nome=os.path.basename(arq), n=k, popt=popt, R2=r2,
                 K=popt[1] / (d["para"] - d["de"]), tau=popt[2], L=popt[3],
                 yn=(p - popt[0]) / popt[1])
        ensaios.append(d)

    if not ensaios:
        sys.exit("Nenhum ensaio válido.")

    # ------------------------------------------------ ajuste global normalizado
    tt = np.concatenate([e["t"][e["t"] >= 0] for e in ensaios])
    yy = np.concatenate([e["yn"][e["t"] >= 0] for e in ensaios])
    (tau_g, L_g), pcov = curve_fit(degrau_normalizado, tt, yy,
                                   p0=[np.median([e["tau"] for e in ensaios]),
                                       np.median([e["L"] for e in ensaios])],
                                   bounds=([0.005, 0.0], [30, 5]))
    sd_g = np.sqrt(np.diag(pcov))
    yh = degrau_normalizado(tt, tau_g, L_g)
    r2_g = 1 - np.sum((yy - yh) ** 2) / np.sum((yy - yy.mean()) ** 2)

    # ------------------------------------------------ tabela
    print(f"\n{'ensaio':<16} {'p0 bar':>7} {'Δp bar':>7} {'K bar/%':>9} {'τ s':>6} "
          f"{'L s':>6} {'R²':>6} | {'K Smith':>8} {'τ Sm':>6} {'L Sm':>6}")
    for e in ensaios:
        fw = e["fw"]
        print(f"{e['nome']:<16} {e['popt'][0]:7.4f} {e['popt'][1]:7.4f} {e['K']:9.5f} "
              f"{e['tau']:6.3f} {e['L']:6.3f} {e['R2']:6.3f} | {fw.get('K', np.nan):8.5f} "
              f"{fw.get('tau', np.nan):6.3f} {fw.get('L', np.nan):6.3f}")
    K = np.array([e["K"] for e in ensaios])
    T = np.array([e["tau"] for e in ensaios])
    L = np.array([e["L"] for e in ensaios])
    print(f"\nMédia ± desvio ({len(ensaios)} ensaios):  K = {K.mean():.5f} ± {K.std(ddof=1):.5f} bar/%"
          f"   τ = {T.mean():.3f} ± {T.std(ddof=1):.3f} s   L = {L.mean():.3f} ± {L.std(ddof=1):.3f} s")
    print(f"Ajuste único na forma normalizada:  τ = {tau_g:.3f} ± {sd_g[0]:.3f} s   "
          f"L = {L_g:.3f} ± {sd_g[1]:.3f} s   R² = {r2_g:.3f}")
    print(f"Modelo representativo:  G(s) = {K.mean():.5f}·e^(-{L_g:.3f}s) / ({tau_g:.3f}s + 1)  [bar/%]")

    saida = os.path.join(args.pasta, "degraus_mq")
    with open(saida + "_parametros.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["ensaio", "p0_bar", "dp_bar", "K_bar_por_pct", "tau_s", "L_s", "R2",
                    "K_smith", "tau_smith", "L_smith"])
        for e in ensaios:
            fw = e["fw"]
            w.writerow([e["nome"], f"{e['popt'][0]:.5f}", f"{e['popt'][1]:.5f}", f"{e['K']:.6f}",
                        f"{e['tau']:.4f}", f"{e['L']:.4f}", f"{e['R2']:.4f}",
                        fw.get("K", ""), fw.get("tau", ""), fw.get("L", "")])
        w.writerow(["GLOBAL_normalizado", "", "", f"{K.mean():.6f}", f"{tau_g:.4f}",
                    f"{L_g:.4f}", f"{r2_g:.4f}", "", "", ""])

    # ------------------------------------------------ figura 1: os ensaios, um a um
    n = len(ensaios)
    ncol = 5
    nlin = int(np.ceil(n / ncol))
    fig, axs = plt.subplots(nlin, ncol, figsize=(3.4 * ncol, 2.6 * nlin),
                            sharex=True, sharey=True, squeeze=False)
    for k, e in enumerate(ensaios):
        a = axs[k // ncol][k % ncol]
        sel = e["t"] >= -1.0
        a.plot(e["t"][sel], e["p"][sel], ".", ms=2.5, color=COR_DADOS, label="medido")
        tm = np.linspace(0, e["t"].max(), 400)
        a.plot(tm, fopdt(tm, *e["popt"]), lw=2, color=COR_MODELO, label="ajuste MQ")
        a.axvline(0, color=COR_DADOS, lw=0.8, ls=":")
        a.set_title(f"{e['nome'].replace('_dr.log', '')}   τ={e['tau']:.3f}s  L={e['L']:.3f}s\n"
                    f"K={e['K']*1000:.3f}e-3 bar/%   R²={e['R2']:.3f}", fontsize=8)
        a.grid(alpha=0.25)
        a.spines[["top", "right"]].set_visible(False)
        if k == 0:
            a.legend(frameon=False, fontsize=7, loc="lower right")
    for k in range(n, nlin * ncol):
        axs[k // ncol][k % ncol].axis("off")
    for a in axs[-1]:
        a.set_xlabel("t desde o degrau (s)")
    for a in axs[:, 0]:
        a.set_ylabel("pressão (bar)")
    fig.suptitle(f"Degrau {ensaios[0]['de']:.0f} → {ensaios[0]['para']:.0f} %: "
                 f"{n} ensaios e ajuste FOPDT por mínimos quadrados", fontsize=11)
    fig.tight_layout()
    fig.savefig(saida + "_experimentos.png", dpi=150)
    plt.close(fig)

    # ------------------------------------------------ figura 2: sobrepostos e normalizados
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(13, 4.8))
    for e in ensaios:
        sel = e["t"] >= -1.0
        a1.plot(e["t"][sel], e["p"][sel], lw=1, color=COR_DADOS, alpha=0.35)
        a2.plot(e["t"][sel], e["yn"][sel], lw=1, color=COR_DADOS, alpha=0.35)
    a1.plot([], [], lw=1, color=COR_DADOS, alpha=0.6, label=f"{n} ensaios")
    a1.set_ylabel("pressão (bar)")
    a1.set_title("Respostas medidas")

    # faixa média ± desvio da forma normalizada (amostras alinhadas a 20 ms)
    grade = np.round(np.arange(-1.0, min(e["t"].max() for e in ensaios), 0.02), 3)
    mat = np.array([np.interp(grade, e["t"], e["yn"]) for e in ensaios])
    m, s = mat.mean(0), mat.std(0, ddof=1)
    a2.fill_between(grade, m - s, m + s, color=COR_MODELO, alpha=0.15, lw=0,
                    label="média ± 1 desvio")
    tm = np.linspace(-1.0, grade[-1], 600)
    a2.plot(tm, degrau_normalizado(tm, tau_g, L_g), lw=2.2, color=COR_MODELO,
            label=f"MQ global: τ={tau_g:.3f} s, L={L_g:.3f} s (R²={r2_g:.3f})")
    a2.plot([], [], lw=1, color=COR_DADOS, alpha=0.6, label=f"{n} ensaios normalizados")
    a2.axhline(1, color=COR_DADOS, lw=0.6)
    a2.axhline(0.632, color=COR_DADOS, lw=0.6, ls="--")
    a2.text(0.99, 0.632, " 63,2 %", va="center", ha="right", fontsize=8,
            color=COR_DADOS, backgroundcolor="white", transform=a2.get_yaxis_transform())
    a2.set_ylabel("(p − p0) / Δp")
    a2.set_title("Forma normalizada")

    for a in (a1, a2):
        a.axvline(0, color=COR_DADOS, lw=0.8, ls=":")
        a.set_xlabel("t desde o degrau (s)")
        a.grid(alpha=0.25)
        a.spines[["top", "right"]].set_visible(False)
        a.legend(frameon=False, fontsize=8, loc="lower right")
    fig.tight_layout()
    fig.savefig(saida + "_normalizado.png", dpi=150)

    # zoom na dinâmica
    for a in (a1, a2):
        a.set_xlim(-0.3, 1.5)
    fig.savefig(saida + "_normalizado_zoom.png", dpi=150)
    plt.close(fig)

    n_am = escrever_log_normalizado(ensaios, saida + "_normalizado_dr.log", tau_g, L_g)

    print(f"\nSalvo: {saida}_parametros.csv, {saida}_experimentos.png, "
          f"{saida}_normalizado.png, {saida}_normalizado_zoom.png, "
          f"{saida}_normalizado_dr.log ({n_am} amostras a 20 ms)")


if __name__ == "__main__":
    main()
