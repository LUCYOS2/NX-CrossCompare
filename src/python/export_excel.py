"""C++ 코어가 만든 비교 결과 CSV를 서식 있는 Excel(.xlsx)로 변환한다.

역할 분리(개발계획_v2.md §2): 계산은 C++(Rule Engine), 엑셀 서식/리포트는 Python.
CSV 컬럼: rule, inch, value_mm, delta_from_baseline_mm, within_tolerance

사용법:
    python export_excel.py comparison_export.csv comparison_export.xlsx

주의: 개인PC에 Python/openpyxl이 설치되어 있지 않아 아직 실행 검증을 하지 못한 초안입니다.
회사PC(또는 Python 설치 후)에서 처음 실행할 때 한 번 동작 확인이 필요합니다.
"""

import csv
import sys

try:
    from openpyxl import Workbook
    from openpyxl.styles import Font, PatternFill
except ImportError as exc:
    raise SystemExit(
        "openpyxl이 필요합니다. 'pip install openpyxl' 실행 후 다시 시도하세요."
    ) from exc

PASS_FILL = PatternFill(start_color="C8FFC8", end_color="C8FFC8", fill_type="solid")
FAIL_FILL = PatternFill(start_color="FFC8C8", end_color="FFC8C8", fill_type="solid")
HEADER_FONT = Font(bold=True)


def convert(csv_path: str, xlsx_path: str) -> None:
    with open(csv_path, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    wb = Workbook()
    ws = wb.active
    ws.title = "치수 비교"

    headers = ["규칙", "인치", "측정값(mm)", "기준선 대비 편차(mm)", "공차 통과"]
    ws.append(headers)
    for cell in ws[1]:
        cell.font = HEADER_FONT

    for row in rows:
        within_tolerance = row["within_tolerance"] == "TRUE"
        ws.append([
            row["rule"],
            int(row["inch"]),
            float(row["value_mm"]),
            float(row["delta_from_baseline_mm"]),
            "PASS" if within_tolerance else "FAIL",
        ])
        fill = PASS_FILL if within_tolerance else FAIL_FILL
        for cell in ws[ws.max_row]:
            cell.fill = fill

    for column_cells in ws.columns:
        max_length = max(len(str(cell.value)) for cell in column_cells)
        ws.column_dimensions[column_cells[0].column_letter].width = max_length + 2

    wb.save(xlsx_path)
    print(f"저장 완료: {xlsx_path}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("사용법: python export_excel.py <입력.csv> <출력.xlsx>")
    convert(sys.argv[1], sys.argv[2])
