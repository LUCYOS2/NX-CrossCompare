# 규칙 카탈로그 (Rule Catalog)

> 이 문서는 살아있는 문서입니다. 회사PC에서 실제 모델을 다루며 새로운 측정 규칙을
> 발견할 때마다 이 문서에 계속 추가합니다. (참고: `개발계획_v2.md` §7, §13 Phase4)
>
> 각 규칙은 Anchor / Reference Frame / Selector / Measurement 4가지로 구성됩니다.
> 스키마 정의는 `개발계획_v2.md` §7 참고.

---

## 규칙 목록

### 001. Boss-Screw 체결 정렬
```
anchor_A: Hole(Ø2.8, part=Bezel)
anchor_B: Boss_Center(Ø2.6, part=Rear_Chassis)
reference_frame: [World_Origin, Datum_CSYS]
selector: nearest_pair
measurement: point_to_point, projection=3D, tol=±0.15
```

### 002. 후크 높이
```
anchor: Hook_Tip_Edge(part=Side_Frame)
reference_plane: Datum_Plane(Rear_Chassis_Base)
selector: leftmost
measurement: point_to_plane, projection=normal, tol=±0.1
```

### 003. 후크 걸림량 (전장길이류)
```
anchor_A: Hook_Catch_Edge(Side_Frame)
anchor_B: Wall_Inner_Edge(Front_Bezel)
reference_frame: [World_Origin]
measurement: axis_projection, projection=Z, tol=+0.3/-0.1  // 비대칭 공차
```

### 004. Rib-OpenCell Gap
```
anchor_A: Face(Rib_Top_Surface)
anchor_B: Face(OpenCell_Edge)
selector: nearest_face_pair
measurement: face_to_face_gap, tol=±0.2
```
> Phase4b 대상 (face 기반, C++ 테셀레이션 알고리즘 필요)

### 005. 살두께 (Boss Root Wall Thickness)
```
anchor_A: Face(Boss_Outer_Wall)
anchor_B: Face(Boss_Inner_Wall)
selector: parallel_face_pair
measurement: face_to_face_gap, projection=normal, tol=±0.05
```
> Phase4b 대상 (face 기반, C++ 테셀레이션 알고리즘 필요)

---

## 신규 규칙 추가 템플릿

```
### NNN. 규칙 이름
anchor_A: <anchor_type>(<param>, part=<part_name>)
anchor_B: <anchor_type>(<param>, part=<part_name>)   // point_to_plane/axis_projection이면 생략 가능
reference_frame: [World_Origin, ...]                 // fallback 필요시 BLU_Center, Datum_CSYS 등 추가
selector: <selector_enum>
measurement: <measurement_type>, projection=<3D|X|Y|Z|normal>, tol=±<value> 또는 +<plus>/-<minus>
```

## 변경 이력

| 날짜 | 내용 |
|---|---|
| 2026-07-08 | 초기 5개 규칙 등록 (개발계획_v2.md에서 이전) |
