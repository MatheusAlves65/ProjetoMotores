"""
Identificação da planta posição da válvula -> pressão, a partir do log do
Monitor Serial (logs/ gerados pelo log2file).

A planta vista pelo PID de pressão é:  posição comandada (%)  ->  pressão (bar).
Ela inclui a malha interna de posição, o escoamento pela borboleta, o volume
da tubulação, o sensor e o filtro, e depende do ponto de operação (vazão).

Formatos de log aceitos
  atual:   T,tempo_ms,pressao_bar,pos_%             + mensagens dos comandos
           'ensaio' (linhas E,) e 'degrau' (# DEGRAU ... e linha D,)
  antigo:  T,tempo_ms,modo,setpoint,pressao,...     (14 colunas, patamares por ref)

Comandos
  mapa     usa as linhas E, do comando 'ensaio' do firmware (média de cada ponto).
           Saída: curva estática p(pos), ajuste semi-físico, ganho local dp/dpos
           e histerese (subida x descida).

  degraus  usa cada comando 'degrau <de> <para>' do firmware.
           Ajusta, por mínimos quadrados na curva inteira, o modelo
                  G(s) = K·e^(-L·s) / (τ·s + 1)
           compara com o método de Smith calculado no ESP32 (linha D,),
           agrupa as repetições (média ± desvio) e sugere um PI pelo SIMC.

Uso:
  python test/identificacao.py mapa    logs/bancada.log --rotulo "3 aspiradores"
  python test/identificacao.py degraus logs/bancada.log
  python test/identificacao.py degraus logs/bancada.log --tauc 0.5

Requer: numpy, scipy, matplotlib
"""

import argparse
import csv
import json
import os
import re
import sys

import numpy as np
from scipy.optimize import curve_fit

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

COLUNAS_COMPLETA = ["t_ms", "modo", "sp", "p", "temp", "ref", "pos", "pwm", "i",
                    "P", "I", "D", "p_bruta"]
COR_DADOS = "#4a4a4a"   # pontos medidos (neutro)
COR_MODELO = "#2a6fdb"  # ajuste

RE_DEGRAU = re.compile(r"# DEGRAU: ([-\d.]+) -> ([-\d.]+) % agora "
                       r"\(p0 = ([-\d.]+) bar(?:, t = (\d+) ms)?\)")
CAMPOS_D = ["de", "para", "p0", "pf", "dp", "K", "t28", "t63", "tau", "L"]


# ---------------------------------------------------------------- leitura
def _num(s):
    try:
        return float(s)
    except ValueError:
        return np.nan


def ler_log(caminho):
    """Lê o log inteiro, na ordem. Retorna (segmentos, degraus, pontos_ensaio).
    Um segmento novo começa a cada reinício do ESP32 (millis() volta)."""
    segs, atual, t_ant = [], [], -1.0
    degraus, ensaio = [], []
    deg_atual = None

    with open(caminho, encoding="utf-8", errors="ignore") as f:
        for linha in f:
            s = linha.strip()

            if s.startswith("T,"):
                c = s.split(",")[1:]
                if not c or not c[0].isdigit():
                    continue                              # cabeçalho ou linha cortada
                if len(c) in (3, 4):                      # T,tempo,pressao,pos[,pwm]
                    reg = {"t_ms": _num(c[0]), "p": _num(c[1]), "pos": _num(c[2]),
                           "pwm": _num(c[3]) if len(c) == 4 else np.nan}
                elif len(c) >= 12:
                    reg = {"t_ms": _num(c[0]), "modo": c[1]}
                    for k, nome in enumerate(COLUNAS_COMPLETA[2:], start=2):
                        reg[nome] = _num(c[k]) if k < len(c) else np.nan
                else:
                    continue
                if reg["t_ms"] < t_ant and atual:         # reinício
                    segs.append(atual)
                    atual = []
                t_ant = reg["t_ms"]
                atual.append(reg)
                if deg_atual and deg_atual["t0_ms"] is None:
                    deg_atual["t0_ms"] = reg["t_ms"]      # firmware antigo: sem t na mensagem
                continue

            m = RE_DEGRAU.match(s)
            if m:
                deg_atual = {"de": float(m.group(1)), "para": float(m.group(2)),
                             "p0_fw": float(m.group(3)),
                             "t0_ms": float(m.group(4)) if m.group(4) else None,
                             "seg": len(segs), "t_fim_ms": None,
                             "motivo": "", "fw": None}
                degraus.append(deg_atual)
                continue

            if s.startswith("D,") and degraus:
                v = s.split(",")[1:]
                if len(v) == len(CAMPOS_D) and v[0] != "de_%":
                    degraus[-1]["fw"] = {k: _num(x) for k, x in zip(CAMPOS_D, v)}
                continue

            if s.startswith("# DEGRAU terminado") and deg_atual:
                deg_atual["motivo"] = s
                deg_atual["t_fim_ms"] = t_ant
                deg_atual = None
                continue

            if s.startswith("E,"):
                c = s.split(",")
                if len(c) == 8 and c[1].isdigit():
                    q = {"ref": _num(c[3]), "pos_est": _num(c[4]), "p": _num(c[5]),
                         "p_desvio": _num(c[6]), "sentido": c[2]}
                    if np.isfinite(q["p"]):
                        ensaio.append(q)

    if atual:
        segs.append(atual)

    saida = []
    for seg in segs:
        chaves = seg[0].keys()
        d = {k: np.array([r.get(k, np.nan) for r in seg]) for k in chaves}
        d["t"] = (d["t_ms"] - d["t_ms"][0]) / 1000.0
        d["completo"] = "ref" in chaves
        saida.append(d)
    return saida, degraus, ensaio


def verificar_amostragem(d):
    dt = np.diff(d["t"])
    if len(dt) and np.median(dt) > 0.05:
        print(f"AVISO: telemetria a cada {np.median(dt)*1000:.0f} ms. "
              "Para identificar a dinâmica use 't 20' (o 'degrau' já faz isso).")


def patamares(d, modo="M", t_min=0.5):
    """(log antigo) Trechos contíguos com o mesmo ref em MANUAL. Lista de (i0, i1)."""
    ok = d["modo"] == modo
    ref = d["ref"]
    res, i0 = [], None
    for k in range(len(ref)):
        novo = (i0 is None) or (not ok[k]) or (ref[k] != ref[i0])
        if novo:
            if i0 is not None and d["t"][k - 1] - d["t"][i0] >= t_min:
                res.append((i0, k - 1))
            i0 = k if ok[k] else None
    if i0 is not None and d["t"][-1] - d["t"][i0] >= t_min:
        res.append((i0, len(ref) - 1))
    return res


# ---------------------------------------------------------------- mapa estático
def modelo_orificio(x, p0, k, eps):
    """Semi-físico: Δp ∝ 1/A², área livre da borboleta A ∝ (1 - sen φ) + vazamento.
    x = % de fechamento, φ = 90°·x/100. Aproximação incompressível."""
    phi = 0.5 * np.pi * x / 100.0
    return p0 + k / (eps + 1.0 - np.sin(phi)) ** 2


def cmd_mapa(args):
    segs, _, pontos = ler_log(args.log)
    if pontos:
        print(f"Usando {len(pontos)} pontos das linhas E, (comando 'ensaio').")
    else:                                            # log antigo, telemetria completa
        for d in segs:
            if not d["completo"]:
                continue
            verificar_amostragem(d)
            for i0, i1 in patamares(d, t_min=args.t_estab + args.t_media):
                t = d["t"]
                idx = np.arange(len(t))
                sel = (t >= t[i1] - args.t_media) & (idx >= i0) & (idx <= i1)
                pontos.append({"ref": float(d["ref"][i0]),
                               "pos_est": float(np.mean(d["pos"][sel])),
                               "p": float(np.mean(d["p"][sel])),
                               "p_desvio": float(np.std(d["p"][sel])),
                               "sentido": "-"})
    if len(pontos) < 3:
        sys.exit("Poucos pontos. Rode o comando 'ensaio' no firmware (linhas E,).")

    pontos.sort(key=lambda q: q["ref"])
    x = np.array([q["ref"] for q in pontos])
    y = np.array([q["p"] for q in pontos])
    xu = np.unique(x)                                # subida e descida: média por posição
    yu = np.array([y[x == r].mean() for r in xu])

    ajuste = None
    try:
        p0 = [y.min(), max(y.min(), 1e-3) * 0.1 + 1e-3, 0.1]
        popt, _ = curve_fit(modelo_orificio, x, y, p0=p0,
                            bounds=([-1.0, 0.0, 1e-3], [5.0, 100.0, 5.0]), maxfev=20000)
        yhat = modelo_orificio(x, *popt)
        r2 = 1 - np.sum((y - yhat) ** 2) / max(np.sum((y - y.mean()) ** 2), 1e-12)
        ajuste = {"p0": popt[0], "k": popt[1], "eps": popt[2], "R2": r2}
    except Exception as e:
        print(f"Ajuste semi-físico falhou ({e}); segue só a tabela.")

    ganho_u = np.gradient(yu, xu) if len(xu) >= 2 else np.full_like(xu, np.nan)
    ganho_de = dict(zip(xu, ganho_u))

    print(f"\nMapa estático  ({args.rotulo or args.log})")
    print(f"{'pos %':>6} {'sentido':>9} {'pos est %':>9} {'p bar':>8} {'desvio':>7} {'dp/dpos bar/%':>14}")
    for q in pontos:
        g = ganho_de[q["ref"]]
        q["ganho_local"] = float(g)
        print(f"{q['ref']:6.1f} {q['sentido']:>9} {q['pos_est']:9.1f} {q['p']:8.4f} "
              f"{q['p_desvio']:7.4f} {g:14.5f}")
    hist = [(r, y[x == r].max() - y[x == r].min()) for r in xu if (x == r).sum() >= 2]
    if hist:
        r_max, h_max = max(hist, key=lambda h: h[1])
        print(f"Histerese máxima (subida x descida): {h_max:.4f} bar em {r_max:.0f} %")
    gpos = ganho_u[ganho_u > 1e-7]
    if len(gpos) >= 2:
        print(f"Razão entre maior e menor ganho local: {gpos.max()/gpos.min():.1f}x "
              "(acima de ~3x, um PI fixo não serve bem a faixa toda)")
    if ajuste:
        print(f"Ajuste p = p0 + k/(eps + 1 - sen φ)²:  p0={ajuste['p0']:.4f}  "
              f"k={ajuste['k']:.5f}  eps={ajuste['eps']:.4f}  R²={ajuste['R2']:.4f}")

    base = os.path.splitext(args.saida or args.log)[0] + "_mapa"
    with open(base + ".json", "w", encoding="utf-8") as f:
        json.dump({"rotulo": args.rotulo, "pontos": pontos, "ajuste": ajuste}, f, indent=2)

    fig, (a1, a2) = plt.subplots(2, 1, figsize=(7, 7), sharex=True)
    for sentido, marca in (("fechando", "o"), ("abrindo", "s"), ("-", "o")):
        qs = [q for q in pontos if q["sentido"] == sentido]
        if not qs:
            continue
        rot = "medido" if sentido == "-" else f"medido, {sentido}"
        a1.errorbar([q["ref"] for q in qs], [q["p"] for q in qs],
                    yerr=[q["p_desvio"] for q in qs], fmt=marca, ms=6,
                    mfc="none" if sentido == "abrindo" else COR_DADOS,
                    color=COR_DADOS, capsize=3, label=rot + " (média ± desvio)")
    if ajuste:
        xx = np.linspace(0, max(x.max(), 1), 300)
        a1.plot(xx, modelo_orificio(xx, ajuste["p0"], ajuste["k"], ajuste["eps"]),
                lw=2, color=COR_MODELO, label=f"ajuste semi-físico (R²={ajuste['R2']:.3f})")
    a1.set_ylabel("pressão (bar)")
    a1.set_title(f"Mapa estático – {args.rotulo or os.path.basename(args.log)}")
    a1.legend(frameon=False)
    a2.plot(xu, ganho_u, "o-", lw=2, ms=6, color=COR_DADOS)
    a2.set_ylabel("ganho local dp/dpos (bar/%)")
    a2.set_xlabel("posição comandada (% de fechamento)")
    for a in (a1, a2):
        a.grid(alpha=0.25)
        a.spines[["top", "right"]].set_visible(False)
    fig.tight_layout()
    fig.savefig(base + ".png", dpi=150)
    print(f"\nSalvo: {base}.json e {base}.png")


# ---------------------------------------------------------------- degraus (FOPDT)
def fopdt(t, y0, dy, tau, L):
    s = np.clip(t - L, 0, None)
    return y0 + dy * (1 - np.exp(-s / tau))


def simc_pi(K, tau, L, tauc):
    """SIMC (Skogestad, 2003) para FOPDT. K em bar/%, saída do PID em %."""
    kc = tau / (K * (tauc + L))
    ti = min(tau, 4 * (tauc + L))
    return kc, ti, kc / ti


def ajustar_fopdt(tt, yy, y0, ruido):
    """Mínimos quadrados na curva inteira. Retorna (popt, R²) ou None."""
    yf = float(np.mean(yy[-max(5, len(yy) // 5):]))
    dy0 = yf - y0
    if abs(dy0) < 3 * ruido + 1e-4:
        return None
    frac = (yy - y0) / dy0
    L0 = tt[np.argmax(frac > 0.1)] if np.any(frac > 0.1) else 0.02
    T63 = tt[np.argmax(frac > 0.63)] if np.any(frac > 0.63) else 0.5
    popt, _ = curve_fit(fopdt, tt, yy, p0=[y0, dy0, max(T63 - L0, 0.02), L0],
                        bounds=([-5, -10, 0.005, 0.0], [10, 10, 30, 5]), maxfev=20000)
    yh = fopdt(tt, *popt)
    r2 = 1 - np.sum((yy - yh) ** 2) / max(np.sum((yy - yy.mean()) ** 2), 1e-12)
    return popt, r2


def degraus_do_firmware(segs, degraus, args):
    """Degraus feitos com o comando 'degrau' (formato atual)."""
    casos = []
    for dg in degraus:
        rot = f"{dg['de']:.0f}->{dg['para']:.0f} %"
        if "concluido" not in dg["motivo"]:
            print(f"Degrau {rot}: não concluído ({dg['motivo'] or 'log cortado'}), ignorado.")
            continue
        if dg["seg"] >= len(segs) or dg["t0_ms"] is None:
            continue
        d = segs[dg["seg"]]
        verificar_amostragem(d)
        t_ms, t0 = d["t_ms"], dg["t0_ms"]
        t_fim = dg["t_fim_ms"] if dg["t_fim_ms"] is not None else t_ms[-1]
        if args.t_pos is not None:
            t_fim = min(t_fim, t0 + args.t_pos * 1000)
        y_all = d["p_bruta"] if (args.sinal == "bruta" and "p_bruta" in d) else d["p"]
        pre = (t_ms >= t0 - args.t_pre * 1000) & (t_ms < t0)
        jan = (t_ms >= t0) & (t_ms <= t_fim)
        if pre.sum() < 5 or jan.sum() < 10:
            print(f"Degrau {rot}: poucas amostras (use 't 20'), ignorado.")
            continue
        ctx = (t_ms >= t0 - args.t_pre * 1000) & (t_ms <= t_fim)
        casos.append({"de": dg["de"], "para": dg["para"], "fw": dg["fw"],
                      "tt": (t_ms[jan] - t0) / 1000.0, "yy": y_all[jan],
                      "y0": float(np.mean(y_all[pre])), "ruido": float(np.std(y_all[pre])),
                      "ctx": contexto(d, ctx, (t_ms - t0) / 1000.0, y_all)})
    return casos


def contexto(d, sel, t_rel, y_all):
    """Janela completa (antes e depois do degrau) para a figura detalhada."""
    n = len(d["t_ms"])
    vazio = np.full(n, np.nan)
    return {"t": t_rel[sel], "p": y_all[sel],
            "pos": d.get("pos", vazio)[sel], "pwm": d.get("pwm", vazio)[sel]}


def tempo_movimento(ctx):
    """Do degrau até o PWM voltar a zero (a válvula chegou e parou). NaN sem PWM."""
    t, pwm = ctx["t"], ctx["pwm"]
    if np.all(np.isnan(pwm)):
        return np.nan
    dep = t >= 0
    ativo = np.where(dep & (np.abs(pwm) > 0))[0]
    if not len(ativo):
        return np.nan
    parado = np.where(dep & (np.arange(len(t)) > ativo[0]) & (pwm == 0))[0]
    return float(t[parado[0]]) if len(parado) else np.nan


def degraus_de_patamares(segs, args):
    """Log antigo (telemetria completa): degraus entre patamares de ref vizinhos."""
    casos = []
    for d in segs:
        if not d["completo"]:
            continue
        verificar_amostragem(d)
        y_all = d["p"]
        if args.sinal == "bruta" and not np.all(np.isnan(d["p_bruta"])):
            y_all = d["p_bruta"]
        pats = patamares(d, t_min=1.0)
        for (a0, a1), (b0, b1) in zip(pats[:-1], pats[1:]):
            if b0 != a1 + 1:
                continue
            du = d["ref"][b0] - d["ref"][a0]
            if abs(du) < 0.5 or abs(du) > args.du_max:
                continue
            t = d["t"]
            t0 = t[b0]
            t_pos = args.t_pos if args.t_pos is not None else 5.0
            pre = (t >= t0 - args.t_pre) & (t < t0)
            jan = (t >= t0) & (t <= min(t[b1], t0 + t_pos))
            if pre.sum() < 5 or jan.sum() < 10:
                continue
            ctx = (t >= t0 - args.t_pre) & (t <= min(t[b1], t0 + t_pos))
            casos.append({"de": float(d["ref"][a0]), "para": float(d["ref"][b0]), "fw": None,
                          "tt": t[jan] - t0, "yy": y_all[jan],
                          "y0": float(np.mean(y_all[pre])), "ruido": float(np.std(y_all[pre])),
                          "ctx": contexto(d, ctx, t - t0, y_all)})
    return casos


def cmd_degraus(args):
    segs, degraus, _ = ler_log(args.log)
    if degraus:
        print(f"{len(degraus)} comando(s) 'degrau' encontrados no log.")
        casos = degraus_do_firmware(segs, degraus, args)
    else:
        casos = degraus_de_patamares(segs, args)

    resultados, curvas = [], []
    for c in casos:
        rot = f"{c['de']:.0f}->{c['para']:.0f} %"
        try:
            aj = ajustar_fopdt(c["tt"], c["yy"], c["y0"], c["ruido"])
        except Exception as e:
            print(f"Degrau {rot}: ajuste falhou ({e})")
            continue
        if aj is None:
            print(f"Degrau {rot}: resposta menor que 3x o ruído, ignorado. Use um degrau maior.")
            continue
        popt, r2 = aj
        du = c["para"] - c["de"]
        K = popt[1] / du
        tauc = args.tauc if args.tauc is not None else max(popt[3], 0.02)
        kc, ti, ki = simc_pi(K, popt[2], popt[3], tauc)
        fw = c["fw"] or {}
        r = {"de": c["de"], "para": c["para"], "du": du,
             "K": float(K), "tau_s": float(popt[2]), "L_s": float(popt[3]), "R2": float(r2),
             "K_smith": fw.get("K", np.nan), "tau_smith": fw.get("tau", np.nan),
             "L_smith": fw.get("L", np.nan), "t_mov_s": tempo_movimento(c["ctx"]),
             "tauc_s": float(tauc), "kp": float(kc), "ti_s": float(ti), "ki": float(ki)}
        resultados.append(r)
        curvas.append((c["tt"], c["yy"], fopdt(c["tt"], *popt), r, c, popt))

    if not resultados:
        sys.exit("Nenhum degrau válido. Use o comando 'degrau <de> <para>' do firmware "
                 "(ou, em log antigo, patamares em MANUAL com telemetria completa a 20 ms).")

    print(f"\nDegraus – G(s) = K·e^(-Ls)/(τs+1)   [MQ = mínimos quadrados na curva inteira; "
          f"Smith = calculado no ESP32]")
    print(f"{'de->para %':>11} | {'K MQ':>9} {'τ MQ':>6} {'L MQ':>6} {'R²':>5} | "
          f"{'K Smith':>9} {'τ Sm':>6} {'L Sm':>6} | {'t mov s':>7} | {'kp %/bar':>9} {'ki':>9}")
    for r in resultados:
        print(f"{r['de']:5.0f}->{r['para']:<5.0f} | {r['K']:9.5f} {r['tau_s']:6.3f} "
              f"{r['L_s']:6.3f} {r['R2']:5.2f} | {r['K_smith']:9.5f} {r['tau_smith']:6.3f} "
              f"{r['L_smith']:6.3f} | {r['t_mov_s']:7.3f} | {r['kp']:9.1f} {r['ki']:9.1f}")
        if r["R2"] < 0.8:
            print("   (R² baixo: resposta perto do ruído. Repita com degrau maior; NÃO use esta sintonia)")
        elif r["tau_s"] < 5 * 0.02:
            print("   (τ < 5·Ts do PID de 20 ms: a discretização passa a pesar)")

    # repetições: média ± desvio por (de, para)
    grupos = {}
    for r in resultados:
        if r["R2"] >= 0.8:
            grupos.setdefault((r["de"], r["para"]), []).append(r)
    if any(len(g) >= 2 for g in grupos.values()):
        print(f"\nRepetições (só R² >= 0,8): média ± desvio")
        print(f"{'de->para %':>11} {'n':>2} | {'K bar/%':>19} | {'τ s':>15} | {'L s':>15}")
        for (de, para), g in sorted(grupos.items()):
            K = np.array([r["K"] for r in g])
            T = np.array([r["tau_s"] for r in g])
            L = np.array([r["L_s"] for r in g])
            print(f"{de:5.0f}->{para:<5.0f} {len(g):2d} | {K.mean():9.5f} ± {K.std(ddof=1) if len(g) > 1 else 0:7.5f} | "
                  f"{T.mean():6.3f} ± {T.std(ddof=1) if len(g) > 1 else 0:6.3f} | "
                  f"{L.mean():6.3f} ± {L.std(ddof=1) if len(g) > 1 else 0:6.3f}")

    base = os.path.splitext(args.saida or args.log)[0] + "_degraus"
    with open(base + ".csv", "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(resultados[0].keys()))
        w.writeheader()
        w.writerows(resultados)

    n = len(curvas)
    ncol = min(3, n)
    nlin = int(np.ceil(n / ncol))
    fig, axs = plt.subplots(nlin, ncol, figsize=(4.2 * ncol, 3.2 * nlin), squeeze=False)
    for k, (tt, yy, yh, r, _, _) in enumerate(curvas):
        a = axs[k // ncol][k % ncol]
        a.plot(tt, yy, ".", ms=3, color=COR_DADOS, label="medido")
        a.plot(tt, yh, lw=2, color=COR_MODELO, label="modelo (MQ)")
        a.set_title(f"{r['de']:.0f}→{r['para']:.0f} %  K={r['K']:.4f}  "
                    f"τ={r['tau_s']:.2f}s  L={r['L_s']:.2f}s", fontsize=9)
        a.set_xlabel("t desde o degrau (s)")
        a.set_ylabel("pressão (bar)")
        a.grid(alpha=0.25)
        a.spines[["top", "right"]].set_visible(False)
        if k == 0:
            a.legend(frameon=False, fontsize=8)
    for k in range(n, nlin * ncol):
        axs[k // ncol][k % ncol].axis("off")
    fig.tight_layout()
    fig.savefig(base + ".png", dpi=150)
    plt.close(fig)
    print(f"\nSalvo: {base}.csv e {base}.png")

    # uma figura detalhada por degrau: pressão, posição e PWM no mesmo eixo de tempo
    for k, (_, _, _, r, c, popt) in enumerate(curvas, start=1):
        nome = f"{base}_{k:02d}_{r['de']:.0f}-{r['para']:.0f}.png"
        figura_detalhe(c, popt, r, nome, args.janela)
        print(f"Salvo: {nome}")


def figura_detalhe(c, popt, r, caminho, janela=None):
    ctx = c["ctx"]
    sel = np.ones(len(ctx["t"]), bool)
    if janela != 0:                                    # zoom onde a dinâmica acontece
        t_fim = janela if janela else max(1.5, 5 * (r["tau_s"] + r["L_s"]),
                                          3 * np.nan_to_num(r["t_mov_s"]))
        sel = (ctx["t"] >= -0.5) & (ctx["t"] <= t_fim)
    ctx = {k: v[sel] for k, v in ctx.items()}
    t = ctx["t"]
    tem_pwm = not np.all(np.isnan(ctx["pwm"]))
    nlin = 3 if tem_pwm else 2
    fig, axs = plt.subplots(nlin, 1, figsize=(8, 2.4 * nlin + 0.6), sharex=True)
    titulo = (f"Degrau {r['de']:.0f} → {r['para']:.0f} %   K = {r['K']:.5f} bar/%   "
              f"τ = {r['tau_s']:.3f} s   L = {r['L_s']:.3f} s")
    if np.isfinite(r["t_mov_s"]):
        titulo += f"   movimento = {r['t_mov_s']:.2f} s"
    fig.suptitle(titulo, fontsize=10)

    # 1) pressão: medida e modelo
    a = axs[0]
    a.plot(t, ctx["p"], ".", ms=3, color=COR_DADOS, label="pressão medida")
    tm = t[t >= 0]
    a.plot(tm, fopdt(tm, *popt), lw=2, color=COR_MODELO, label="modelo K·e^(-Ls)/(τs+1)")
    a.set_ylabel("pressão (bar)")
    a.legend(frameon=False, fontsize=8, loc="best")

    # 2) posição: comandada (degrau) e estimada pelo firmware
    a = axs[1]
    a.step([t[0], 0, t[-1]], [c["de"], c["para"], c["para"]], where="post",
           lw=1.5, ls="--", color=COR_DADOS, label="posição comandada")
    a.plot(t, ctx["pos"], lw=2, color=COR_MODELO, label="posição estimada")
    a.set_ylabel("posição (% fech.)")
    a.legend(frameon=False, fontsize=8, loc="best")

    # 3) PWM aplicado na ponte H
    if tem_pwm:
        a = axs[2]
        a.step(t, ctx["pwm"], where="post", lw=1.5, color=COR_DADOS, label="PWM aplicado")
        a.axhline(0, color=COR_DADOS, lw=0.6)
        a.set_ylabel("PWM (+ fecha, − abre)")
        a.legend(frameon=False, fontsize=8, loc="best")

    for a in axs:
        a.axvline(0, color=COR_DADOS, lw=0.8, ls=":")
        a.grid(alpha=0.25)
        a.spines[["top", "right"]].set_visible(False)
    axs[-1].set_xlabel("t desde o degrau (s)")
    fig.tight_layout()
    fig.savefig(caminho, dpi=150)
    plt.close(fig)


# ---------------------------------------------------------------- main
def main():
    try:  # console do Windows (cp1252) não tem τ, φ, ²
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for nome in ("mapa", "degraus"):
        s = sub.add_parser(nome)
        s.add_argument("log")
        s.add_argument("--saida", help="prefixo dos arquivos de saída (padrão: o do log)")
        s.add_argument("--rotulo", default="", help="ponto de operação, ex.: '3 aspiradores'")
    sub.choices["mapa"].add_argument("--t-estab", type=float, default=3.0,
                                     help="(log antigo) s descartados no início de cada patamar")
    sub.choices["mapa"].add_argument("--t-media", type=float, default=2.0,
                                     help="(log antigo) s de média no fim de cada patamar")
    g = sub.choices["degraus"]
    g.add_argument("--sinal", choices=["filtrada", "bruta"], default="filtrada",
                   help="pressão usada; 'bruta' só existe no log antigo [filtrada = a do PID]")
    g.add_argument("--t-pre", type=float, default=2.0, help="s antes do degrau para p inicial")
    g.add_argument("--t-pos", type=float, default=None,
                   help="s após o degrau usados no ajuste [padrão: todo o registro]")
    g.add_argument("--du-max", type=float, default=15.0, help="(log antigo) maior degrau aceito (%%)")
    g.add_argument("--janela", type=float, default=None,
                   help="s após o degrau nas figuras detalhadas [padrão: automático, "
                        "~5·(τ+L)]; 0 = registro inteiro")
    g.add_argument("--tauc", type=float, default=None,
                   help="τc do SIMC (s). Padrão: τc = L (sintonia 'apertada'); "
                        "aumente para mais robustez")
    args = ap.parse_args()
    {"mapa": cmd_mapa, "degraus": cmd_degraus}[args.cmd](args)


if __name__ == "__main__":
    main()
