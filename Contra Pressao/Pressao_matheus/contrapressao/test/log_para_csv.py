"""
Converte um log do Monitor Serial (linhas "T,") em CSV.

Colunas geradas:
  tempo_ms           millis() do ESP32
  t_s                segundos desde o degrau (negativo = antes); sem degrau no log,
                     segundos desde a primeira amostra
  pressao_bar        pressão filtrada
  pos_estimada_pct   posição estimada da válvula (% de fechamento)
  pwm                PWM aplicado na ponte H (+ fecha, - abre); vazio em log antigo
  pos_comandada_pct  posição pedida pelo comando 'degrau' (vazio sem degrau)

Uso:
  python test/log_para_csv.py test/log_01.log                 -> test/log_01.csv
  python test/log_para_csv.py test/log_01.log --excel         -> separador ';' e vírgula decimal
  python test/log_para_csv.py logs/*.log                      -> um CSV por log
"""

import argparse
import csv
import glob
import os
import re
import sys

RE_DEGRAU = re.compile(r"# DEGRAU: ([-\d.]+) -> ([-\d.]+) % agora .*?t = (\d+) ms")


def converter(caminho, excel=False):
    linhas, degrau = [], None
    with open(caminho, encoding="utf-8", errors="ignore") as f:
        for s in f:
            s = s.strip()
            m = RE_DEGRAU.match(s)
            if m and degrau is None:                       # usa o primeiro degrau do log
                degrau = (float(m.group(1)), float(m.group(2)), int(m.group(3)))
                continue
            if not s.startswith("T,"):
                continue
            c = s.split(",")[1:]
            if not c or not c[0].isdigit() or len(c) not in (3, 4):
                continue
            linhas.append((int(c[0]), c[1], c[2], c[3] if len(c) == 4 else ""))

    if not linhas:
        print(f"{caminho}: nenhuma linha de telemetria (T,) no formato tempo,pressao,pos[,pwm]")
        return None

    t_ref = degrau[2] if degrau else linhas[0][0]
    saida = os.path.splitext(caminho)[0] + ".csv"
    sep = ";" if excel else ","

    def num(x):
        return x.replace(".", ",") if excel else x

    with open(saida, "w", newline="", encoding="utf-8-sig" if excel else "utf-8") as f:
        w = csv.writer(f, delimiter=sep)
        w.writerow(["tempo_ms", "t_s", "pressao_bar", "pos_estimada_pct", "pwm", "pos_comandada_pct"])
        for t_ms, p, pos, pwm in linhas:
            t_s = (t_ms - t_ref) / 1000.0
            cmd = ""
            if degrau:
                cmd = f"{degrau[1] if t_ms >= degrau[2] else degrau[0]:.1f}"
            w.writerow([t_ms, num(f"{t_s:.3f}"), num(p), num(pos), pwm, num(cmd)])

    info = (f"degrau {degrau[0]:.0f} -> {degrau[1]:.0f} % em t = {degrau[2]} ms"
            if degrau else "sem degrau no log")
    print(f"{caminho} -> {saida}  ({len(linhas)} amostras, {info})")
    return saida


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+", help="arquivo(s) .log (aceita curinga, ex.: logs/*.log)")
    ap.add_argument("--excel", action="store_true",
                    help="formato do Excel em português: separador ';' e vírgula decimal")
    args = ap.parse_args()
    arquivos = [a for padrao in args.logs for a in (glob.glob(padrao) or [padrao])]
    for a in arquivos:
        converter(a, args.excel)


if __name__ == "__main__":
    main()
