"""CTP 양식 디자인 샘플 생성기 (A4 세로, 한 시트에 페이지 순서대로)."""
import datetime, os, sys
from PIL import Image as PImage, ImageDraw, ImageFont
from openpyxl import Workbook
from openpyxl.cell.rich_text import CellRichText, TextBlock
from openpyxl.cell.text import InlineFont
from openpyxl.drawing.image import Image as XlImage
from openpyxl.drawing.spreadsheet_drawing import OneCellAnchor, AnchorMarker
from openpyxl.drawing.xdr import XDRPositiveSize2D
from openpyxl.styles import Alignment, Border, Font, PatternFill, Side
from openpyxl.worksheet.pagebreak import Break, RowBreak

OUT = sys.argv[1]
IMGDIR = sys.argv[2]
os.makedirs(IMGDIR, exist_ok=True)

FONT = "맑은 고딕"
KFONT_PATH = "C:/Windows/Fonts/malgun.ttf"
KFONT_BOLD = "C:/Windows/Fonts/malgunbd.ttf"

# ---- 치수(pt) --------------------------------------------------------------
COL_PT = {"A": 46, "B": 44, "C": 112, "D": 64, "E": 86, "F": 92, "G": 92}
PAGE_PT = 768          # A4 297mm - 위 10mm - 아래 15mm(바닥글) 안쪽 높이에 맞춤
ROW_INFO = 15
ROW_TABLE = 15
SPACER = 5
WIDTH_PT = sum(COL_PT.values())

C_LABEL = PatternFill("solid", fgColor="F2F2F2")
C_HEAD = PatternFill("solid", fgColor="DCE6F1")
C_SECTION = PatternFill("solid", fgColor="44546A")
C_CHANGED = PatternFill("solid", fgColor="FFF2CC")
POINT_RED = "C00000"

thin = Side(style="thin", color="808080")
med = Side(style="medium", color="404040")


def col_width(pt):
    # 이 PC의 Excel 실측: 열 너비 1단위 ≈ 6.0pt (디스플레이 배율에 따라 달라짐 → 인쇄는 너비 맞춤으로 보정)
    return round(pt / 6.0, 2)


def f(size=9, bold=False, color="000000"):
    return Font(name=FONT, size=size, bold=bold, color=color)


CENTER = Alignment(horizontal="center", vertical="center", wrap_text=True)
LEFT = Alignment(horizontal="left", vertical="center", wrap_text=True, indent=1)
TOPLEFT = Alignment(horizontal="left", vertical="top", wrap_text=True, indent=1)


def cells(ws, rng):
    if ":" not in rng:
        rng = f"{rng}:{rng}"
    for row in ws[rng]:
        for c in row:
            yield c


def box(ws, rng, value=None, font=None, fill=None, align=CENTER, fmt=None, merge=True):
    first = rng.split(":")[0]
    if merge and ":" in rng:
        ws.merge_cells(rng)
    for c in cells(ws, rng):
        c.border = Border(left=thin, right=thin, top=thin, bottom=thin)
        if fill:
            c.fill = fill
    a = ws[first]
    if value is not None:
        a.value = value
    a.font = font or f()
    a.alignment = align
    if fmt:
        a.number_format = fmt
    return a


def outline(ws, r1, r2, c1="A", c2="G"):
    """블록 바깥 테두리를 굵게."""
    cols = "ABCDEFG"
    i1, i2 = cols.index(c1), cols.index(c2)
    for r in range(r1, r2 + 1):
        for i in range(i1, i2 + 1):
            c = ws[f"{cols[i]}{r}"]
            b = c.border
            c.border = Border(
                left=med if i == i1 else b.left,
                right=med if i == i2 else b.right,
                top=med if r == r1 else b.top,
                bottom=med if r == r2 else b.bottom,
            )


def section(ws, r, left, right=""):
    ws.row_dimensions[r].height = 16
    ws.merge_cells(f"A{r}:D{r}")
    ws.merge_cells(f"E{r}:G{r}")
    for c in cells(ws, f"A{r}:G{r}"):
        c.fill = C_SECTION
    ws[f"A{r}"].value = left
    ws[f"A{r}"].font = f(9, True, "FFFFFF")
    ws[f"A{r}"].alignment = Alignment(horizontal="left", vertical="center", indent=1)
    ws[f"E{r}"].value = right
    ws[f"E{r}"].font = f(8, False, "FFFFFF")
    ws[f"E{r}"].alignment = Alignment(horizontal="right", vertical="center", indent=1)


def tol_text(lo, hi):
    if abs(lo + hi) < 1e-9 and hi > 0:
        return f"±{hi:.2f}"
    return f"{lo:+.2f}/{hi:+.2f}"


# ---- 샘플 이미지 -----------------------------------------------------------
def kfont(size, bold=False):
    return ImageFont.truetype(KFONT_BOLD if bold else KFONT_PATH, size)


def arrow(d, p, q, color, w):
    import math
    d.line([p, q], fill=color, width=w)
    for a, b in ((p, q), (q, p)):
        ang = math.atan2(b[1] - a[1], b[0] - a[0])
        L = 7 * w
        for s in (0.45, -0.45):
            d.line([b, (b[0] - L * math.cos(ang + s), b[1] - L * math.sin(ang + s))], fill=color, width=w)


def label(d, xy, text, color, r):
    x, y = xy
    d.ellipse([x - r, y - r, x + r, y + r], fill="white", outline=color, width=max(2, r // 7))
    fnt = kfont(int(r * 1.3), True)
    d.text((x, y), text, fill=color, font=fnt, anchor="mm")


def draw_hook(d, ox, oy, s, labels, scale_h=1.0, color=(192, 0, 0)):
    """스냅 후크 단면(샘플). ox,oy=좌하단, s=스케일(px/mm)."""
    H = 4.6 * scale_h
    poly = [(0, 0), (8.85, 0), (8.85, 1.5), (5.2, 1.5), (5.2, 1.5 + H - 1.2),
            (6.0, 1.5 + H - 0.9), (5.2, 1.5 + H), (3.7, 1.5 + H), (3.7, 1.5), (0, 1.5)]
    pts = [(ox + x * s, oy - y * s) for x, y in poly]
    d.polygon(pts, fill=(205, 214, 228), outline=(60, 60, 60))
    d.line(pts + [pts[0]], fill=(60, 60, 60), width=max(2, int(s / 12)))
    w = max(2, int(s / 16))
    R = max(18, int(s * 0.5))
    P = lambda x, y: (ox + x * s, oy - y * s)
    gap = 0.9
    if "C" in labels:   # 전장
        arrow(d, P(0, -gap), P(8.85, -gap), color, w)
        d.line([P(0, 0), P(0, -gap - 0.3)], fill=color, width=1)
        d.line([P(8.85, 0), P(8.85, -gap - 0.3)], fill=color, width=1)
        label(d, P(4.4, -gap - 0.55), labels["C"], color, R)
    if "A" in labels:   # 후크 높이
        x = 9.9
        arrow(d, P(x, 1.5), P(x, 1.5 + H), color, w)
        d.line([P(8.85, 1.5), P(x + 0.3, 1.5)], fill=color, width=1)
        d.line([P(5.2, 1.5 + H), P(x + 0.3, 1.5 + H)], fill=color, width=1)
        label(d, P(x + 0.75, 1.5 + H / 2), labels["A"], color, R)
    if "B" in labels:   # 걸림량
        y = 1.5 + H + 0.9
        arrow(d, P(5.2, y), P(6.0, y), color, w)
        d.line([P(6.0, 1.5 + H - 0.9), P(6.0, y + 0.3)], fill=color, width=1)
        d.line([P(5.2, 1.5 + H), P(5.2, y + 0.3)], fill=color, width=1)
        label(d, P(5.6, y + 0.75), labels["B"], color, R)
    if "D" in labels:   # 두께
        y = 1.5 + H * 0.45
        arrow(d, P(3.7, y), P(5.2, y), color, w)
        label(d, P(2.6, y), labels["D"], color, R)


def representative_image(path, w_pt, h_pt, caption):
    k = 3  # 해상도 배율(px/pt)
    W, H = int(w_pt * k), int(h_pt * k)
    im = PImage.new("RGB", (W, H), (250, 250, 250))
    d = ImageDraw.Draw(im)
    s = min(W / 15.0, H / 10.5)
    ox = (W - 11.5 * s) / 2
    oy = H - 2.0 * s
    draw_hook(d, ox, oy, s, {"A": "A", "B": "B", "C": "C", "D": "D"})
    d.text((14, 10), caption, fill=(110, 110, 110), font=kfont(int(H * 0.035) + 10))
    im.save(path)
    return W, H


def all_inch_image(path, w_pt, h_pt, inches):
    k = 3
    W, H = int(w_pt * k), int(h_pt * k)
    im = PImage.new("RGB", (W, H), (250, 250, 250))
    d = ImageDraw.Draw(im)
    cols, rows = 3, 2
    cw, ch = W / cols, H / rows
    for i, inch in enumerate(inches):
        cx, cy = (i % cols) * cw, (i // cols) * ch
        d.rectangle([cx + 6, cy + 6, cx + cw - 6, cy + ch - 6], outline=(170, 170, 170), width=2)
        d.rectangle([cx + 6, cy + 6, cx + 6 + 120, cy + 6 + 52], fill=(68, 84, 106))
        d.text((cx + 66, cy + 32), f'{inch}"', fill="white", font=kfont(34, True), anchor="mm")
        s = min(cw / 14.0, ch / 12.0)
        draw_hook(d, cx + (cw - 11.5 * s) / 2, cy + ch - 2.2 * s, s,
                  {"A": "E", "D": "F"}, scale_h=0.8 + 0.1 * i)
    if len(inches) < cols * rows:
        i = len(inches)
        cx, cy = (i % cols) * cw, (i // cols) * ch
        d.text((cx + cw / 2, cy + ch / 2), "샘플 이미지\n실제는 앱 캡처\n(인치별 치수선·라벨 포함)",
               fill=(120, 120, 120), font=kfont(30), anchor="mm", align="center")
    im.save(path)


def place_image(ws, path, top_row, w_pt, h_pt, img_w_pt, img_h_pt):
    img = XlImage(path)
    emu = 12700
    off_x = (w_pt - img_w_pt) / 2
    off_y = (h_pt - img_h_pt) / 2
    img.anchor = OneCellAnchor(
        _from=AnchorMarker(col=0, colOff=int(off_x * emu), row=top_row - 1, rowOff=int(off_y * emu)),
        ext=XDRPositiveSize2D(int(img_w_pt * emu), int(img_h_pt * emu)),
    )
    ws.add_image(img)


# ---- 문서 데이터(샘플) -----------------------------------------------------
INCHES = [43, 50, 65, 75, 85]
DOC = {
    "model": "KS9000",
    "part": "COVER-DECORATION",
    "codes": {43: "BN61-29348A", 50: "BN61-29349A", 65: "BN61-29350A", 75: "BN61-29351A", 85: "BN61-29352A"},
    "date": datetime.date(2026, 11, 9),
    "writer": "PHY",
    "rev": "1",
    "rev_note": 'REV.1  C 전장 43" 8.80 → 8.85',
    "conditions": [
        ("온도 / 습도\nTemp. / Humidity", "23 ± 2 ℃ / 50 ± 10 %RH"),
        ("측정 장비\nEquipment", "Calipers, Height Gauge (분해능 0.01mm)"),
        ("시료 전처리\nConditioning", "성형 후 상온 24 h 이상 방치"),
        ("시료 수\nSample Q'ty", "인치별 n = 5"),
    ],
}

PAGES = [
    {
        "image": "rep",
        "rep_inch": 65,
        "points": [
            {"pt": "A", "item": "후크 높이\nHook Height", "method": "Height Gauge",
             "rows": {i: (3.20, -0.05, 0.05, "") for i in INCHES}},
            {"pt": "B", "item": "걸림량\nUndercut", "method": "Calipers",
             "rows": {i: (0.80, 0.00, 0.10, "") for i in INCHES}},
            {"pt": "C", "item": "전장\nLength", "method": "Calipers",
             "rows": {43: (8.85, 0.00, 0.05, "REV.1 변경 (8.80→8.85)", True),
                      50: (8.85, 0.00, 0.06, ""), 65: (8.85, 0.00, 0.07, ""),
                      75: (8.85, 0.00, 0.08, ""), 85: (8.85, 0.00, 0.09, "")}},
            {"pt": "D", "item": "두께\nThickness", "method": "Calipers",
             "rows": {i: (1.50, -0.05, 0.05, "") for i in INCHES}},
        ],
    },
    {
        "image": "all",
        "points": [
            {"pt": "E", "item": "리브 높이\nRib Height", "method": "Height Gauge",
             "rows": {43: (5.10, -0.10, 0.10, ""), 50: (5.30, -0.10, 0.10, ""),
                      65: (5.60, -0.10, 0.10, ""), 75: (5.80, -0.10, 0.10, ""),
                      85: (6.00, -0.10, 0.10, "")}},
            {"pt": "F", "item": "리브 두께\nRib Thickness", "method": "Calipers",
             "rows": {i: (1.20, -0.05, 0.05, "") for i in INCHES}},
        ],
    },
]


# ---- 페이지 1장 그리기 -----------------------------------------------------
def build_page(ws, r, page, pno, ptotal):
    start = r
    used = 0

    def h(row, pt):
        nonlocal used
        ws.row_dimensions[row].height = pt
        used += pt

    # 1) 제목 + 결재란
    h(r, 16); h(r + 1, 38)
    t = box(ws, f"A{r}:D{r+1}", align=Alignment(horizontal="left", vertical="center", indent=1))
    t.value = CellRichText(
        TextBlock(InlineFont(rFont=FONT, sz=24, b=True, color="1F3864"), "CTP"),
        TextBlock(InlineFont(rFont=FONT, sz=12, b=True, color="404040"), "   관리 사양서"),
        TextBlock(InlineFont(rFont=FONT, sz=8, color="808080"), "\n     Control Specification"),
    )
    for col, txt in zip("EFG", ["입안  PREPARED", "심사  EXAMINED", "결재  APPROVED"]):
        box(ws, f"{col}{r}", txt, f(8, True), C_LABEL)
        box(ws, f"{col}{r+1}", "")
    outline(ws, r, r + 1, "A", "D"); outline(ws, r, r + 1, "E", "G")
    r += 2
    h(r, SPACER); r += 1

    # 2) 제품/문서 정보
    n = len(INCHES)
    n_info = max(2 + n, 5)
    i0 = r
    for k in range(n_info):
        h(r + k, 22 if k < 2 else ROW_INFO)   # 모델/품명 행은 라벨 2줄이 들어가게
    box(ws, f"A{i0}", "MODEL\n모델", f(7, True), C_LABEL)
    box(ws, f"B{i0}:D{i0}", DOC["model"], f(10, True))
    box(ws, f"A{i0+1}", "PART\n품명", f(7, True), C_LABEL)
    box(ws, f"B{i0+1}:D{i0+1}", DOC["part"], f(10, True))
    box(ws, f"A{i0+2}:A{i0+n_info-1}", "CODE\n품번", f(7, True), C_LABEL)
    for k, inch in enumerate(INCHES):
        box(ws, f"B{i0+2+k}", f'{inch}"', f(9, True), C_LABEL)
        box(ws, f"C{i0+2+k}:D{i0+2+k}", DOC["codes"][inch], f(9))
    for k in range(n, n_info - 2):
        box(ws, f"B{i0+2+k}", ""); box(ws, f"C{i0+2+k}:D{i0+2+k}", "")
    right = [("DATE  작성일", DOC["date"], "yyyy-mm-dd"), ("WRITER  작성자", DOC["writer"], None),
             ("REV.  개정", DOC["rev"], None), ("PAGE  페이지", f"{pno} / {ptotal}", None)]
    for k, (lab, val, fmt) in enumerate(right):
        box(ws, f"E{i0+k}", lab, f(8, True), C_LABEL)
        box(ws, f"F{i0+k}:G{i0+k}", val, f(9), fmt=fmt)
    box(ws, f"E{i0+4}:E{i0+n_info-1}", "REV. NOTE\n개정 내용", f(8, True), C_LABEL)
    box(ws, f"F{i0+4}:G{i0+n_info-1}", DOC["rev_note"], f(8), align=TOPLEFT)
    outline(ws, i0, i0 + n_info - 1, "A", "D"); outline(ws, i0, i0 + n_info - 1, "E", "G")
    r = i0 + n_info
    h(r, SPACER); r += 1

    # 높이 예산: 이미지 칸 = 남는 높이
    n_rows = sum(len(p["rows"]) for p in page["points"])
    tail = 16 + 30 + n_rows * ROW_TABLE + SPACER + 16 + 2 * 26
    img_h = PAGE_PT - used - (16 + SPACER) - tail
    img_h = max(img_h, 120)

    # 3) 측정 위치 이미지
    sub = (f'대표 이미지 : {page["rep_inch"]}"' if page["image"] == "rep"
           else "전 인치 이미지 (인치별 형상 상이)")
    section(ws, r, "MEASURING POINT   측정 위치", sub)
    used += 16; r += 1
    half = round(img_h / 2, 2)
    h(r, half); h(r + 1, img_h - half)
    box(ws, f"A{r}:G{r+1}", "")
    outline(ws, r, r + 1)
    pad = 8
    iw, ih = WIDTH_PT - 2 * pad, img_h - 2 * pad
    path = os.path.join(IMGDIR, f"page{pno}.png")
    if page["image"] == "rep":
        representative_image(path, iw, ih, f'샘플 이미지 - 실제는 앱 캡처 (대표 {page["rep_inch"]}", 치수선·라벨 A~D)')
    else:
        all_inch_image(path, iw, ih, INCHES)
    place_image(ws, path, r, WIDTH_PT, img_h, iw, ih)
    r += 2
    h(r, SPACER); r += 1

    # 4) 관리 항목 표
    section(ws, r, "CHECK POINT   관리 항목", "SPEC. 단위 mm  /  공차 : 하한/상한")
    used += 16; r += 1
    h(r, 30)
    heads = ["INCH", "Point", "Check Point\n관리항목", "SPEC.\n도면 스펙", "Tolerance\n관리공차",
             "Method of\nmeasurement", "Remark\n비고"]
    for col, txt in zip("ABCDEFG", heads):
        box(ws, f"{col}{r}", txt, f(8, True), C_HEAD)
    th = r
    r += 1
    for p in page["points"]:
        g0 = r
        for inch in INCHES:
            spec, lo, hi, rem, *chg = p["rows"][inch]
            changed = bool(chg and chg[0])
            h(r, ROW_TABLE)
            box(ws, f"A{r}", f'{inch}"', f(9))
            fill = C_CHANGED if changed else None
            box(ws, f"D{r}", spec, f(9, changed), fill, fmt="0.00")
            box(ws, f"E{r}", tol_text(lo, hi), f(9, changed), fill)
            box(ws, f"G{r}", rem, f(7, False, "9C5700" if changed else "000000"), fill,
                align=LEFT if rem else CENTER)
            r += 1
        box(ws, f"B{g0}:B{r-1}", p["pt"], f(14, True, POINT_RED))
        box(ws, f"C{g0}:C{r-1}", p["item"], f(9, True))
        box(ws, f"F{g0}:F{r-1}", p["method"], f(9))
        outline(ws, g0, r - 1)
    outline(ws, th, th)
    h(r, SPACER); r += 1

    # 5) 측정 환경 조건
    section(ws, r, "MEASUREMENT CONDITIONS   측정 환경 조건")
    used += 16; r += 1
    c0 = r
    conds = DOC["conditions"]
    for k in range(2):
        h(r, 26)
        (l1, v1), (l2, v2) = conds[2 * k], conds[2 * k + 1]
        box(ws, f"A{r}:B{r}", l1, f(7, True), C_LABEL)
        box(ws, f"C{r}:D{r}", v1, f(8), align=LEFT)
        box(ws, f"E{r}", l2, f(7, True), C_LABEL)
        box(ws, f"F{r}:G{r}", v2, f(8), align=LEFT)
        r += 1
    outline(ws, c0, r - 1)
    return r, used


# ---- 통합 문서 -------------------------------------------------------------
wb = Workbook()
ws = wb.active
ws.title = "CTP"
for col, pt in COL_PT.items():
    ws.column_dimensions[col].width = col_width(pt)

r = 1
for idx, page in enumerate(PAGES, start=1):
    r, used = build_page(ws, r, page, idx, len(PAGES))
    print(f"page {idx}: rows end {r-1}, height {used:.1f}pt / {PAGE_PT}")
    if idx < len(PAGES):
        ws.row_breaks.append(Break(id=r - 1))

last = r - 1
ws.print_area = f"A1:G{last}"
ws.page_setup.paperSize = ws.PAPERSIZE_A4
ws.page_setup.orientation = "portrait"
ws.sheet_properties.pageSetUpPr.fitToPage = True
ws.page_setup.fitToWidth = 1
ws.page_setup.fitToHeight = 0
ws.page_margins.left = ws.page_margins.right = 0.35
ws.page_margins.top = 0.39
ws.page_margins.bottom = 0.59
ws.page_margins.header = 0.2
ws.page_margins.footer = 0.25
ws.print_options.horizontalCentered = True
ws.oddFooter.left.text = f'CTP  {DOC["model"]}  {DOC["part"]}'
ws.oddFooter.left.size = 7
ws.oddFooter.left.font = FONT
ws.oddFooter.right.text = "&P / &N"
ws.oddFooter.right.size = 7
ws.oddFooter.right.font = FONT
ws.sheet_view.showGridLines = False
ws.sheet_view.view = "pageBreakPreview"
ws.sheet_view.zoomScale = 85
ws.sheet_view.zoomScaleSheetLayoutView = 85

# ---- 작성 안내 시트 --------------------------------------------------------
g = wb.create_sheet("작성 안내")
g.sheet_view.showGridLines = False
g.column_dimensions["A"].width = 22
g.column_dimensions["B"].width = 18
g.column_dimensions["C"].width = 70
rows = [
    ("항목", "채우는 방법", "설명"),
    ("모델 / 품명", "작업장", "작업장(모델) 정보에서 가져온다."),
    ("품번 (인치별)", "자동 후보 + 수정", "STEP 파일명/제품명에서 BN61-xxxxxA 형태를 찾아 채우고, 없으면 사용자가 입력한다."),
    ("작성일 / 작성자", "자동", "내보낸 날짜와 앱 사용자 이름."),
    ("입안 / 심사 / 결재", "빈칸", "인쇄 후 서명하는 칸."),
    ("REV. / 개정 내용", "작업장 개정 이력", "도면 개정 차수와 바뀐 치수 요약. 바뀐 셀은 표에서 노란 배경 + 굵게 표시."),
    ("측정 위치 이미지", "자동 캡처", "기본은 대표 인치 1장. 인치별 형상이 다른 페이지는 전 인치 이미지를 격자로 넣는다."),
    ("Point", "규칙", "이미지의 치수선 라벨과 같은 글자(A, B, C …). 문서 전체에서 이어서 매긴다."),
    ("관리항목 / 측정 방법", "규칙", "규칙에 입력한 이름과 측정 방법. 포인트 단위로 세로 병합."),
    ("SPEC. / 관리공차", "규칙 (인치별)", "도면 스펙(mm)과 공차. 공차 표기는 '하한/상한', 대칭이면 '±'."),
    ("비고", "규칙 / 개정 이력", "인치별 비고. 개정으로 바뀐 값은 변경 전→후를 자동 기록."),
    ("측정 환경 조건", "라이브러리 기본값", "현재 값은 예시. 항목 확정 후 라이브러리 기본값 + 문서별 수정으로 운영."),
    ("페이지 구성", "자동", "페이지당 Point 1~4개. 이미지 칸 높이는 표 길이에 따라 남는 공간을 채워 A4 한 장에 맞춘다."),
]
for i, row in enumerate(rows, start=1):
    for j, v in enumerate(row):
        c = g.cell(row=i, column=j + 1, value=v)
        c.font = f(10, i == 1, "FFFFFF" if i == 1 else "000000")
        c.alignment = Alignment(vertical="center", wrap_text=True)
        c.border = Border(left=thin, right=thin, top=thin, bottom=thin)
        if i == 1:
            c.fill = C_SECTION
    g.row_dimensions[i].height = 20 if i == 1 else 32
g.cell(row=len(rows) + 2, column=1, value="※ CTP 시트의 모델·품번·치수·측정 환경 값은 모두 양식 확인용 예시 데이터입니다.").font = f(9, False, "C00000")

wb.save(OUT)
print("saved", OUT)
