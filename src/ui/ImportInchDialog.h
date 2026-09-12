#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

#include <string>
#include <utility>
#include <vector>

class QTableWidget;

namespace ui {

// § 공용부품(ALL) 인식(2026-09-12) - "공용부품일 경우 별도 인치 숫자 입력 없이 ALL(전
// 인치를 의미)로 인식해서 치수 비교 테이블에도 들어갈 수 있게 해달라"는 요청. 실제 inch
// 값으로 쓰이는 "미지정"(0)/스핀박스 범위(0~999)와 절대 겹치지 않는 1000을 그 자리에
// 넣어서 나머지 코드(정렬/필터/맵 키)가 대부분 손댈 필요 없이 "그냥 특이하게 큰 inch
// 값 하나"로 자연스럽게 처리되게 했다 - 0(미지정)과 달리 "값이 있는" 진짜 인치로
// 취급되어 handlesByInch에 포함되고 비교 테이블에도 나온다. 표시할 때만 FormatInchLabel()로
// 숫자 대신 "ALL"로 바꿔 보여준다.
inline constexpr int kAllInchValue = 1000;

// handlesByInch의 int 키(또는 LoadedInch::inch)를 화면에 보여줄 문자열로 바꾼다 -
// kAllInchValue는 "ALL", 나머지는 "43\"" 같은 인치 표기. MainWindow의 비교 테이블
// 헤더/대상 인치 콤보 라벨이 공통으로 쓴다.
QString FormatInchLabel(int inch);

// STEP 파일을 여러 개 한번에 불러올 때, 파일명에서 자동인식한 인치를 확인/수정하는
// 다이얼로그. 사용자 확인 답변 반영: 폴더/파일명 어딘가에 인치 숫자가 보통 들어있어서
// 자동인식을 시도하되, 실패/애매하면 0(미지정)으로 비워두고 여기서 직접 입력하게 한다.
// § 공용부품(ALL) - "공용(ALL)" 체크박스를 체크하면 인치 스핀박스 대신 kAllInchValue를
// 결과로 낸다(모든 인치에 공통으로 쓰이는 부품이라 특정 인치 숫자가 없는 경우).
class ImportInchDialog : public QDialog {
    Q_OBJECT

public:
    ImportInchDialog(const QStringList& filePaths, QWidget* parent = nullptr);

    // Accepted 이후 호출. (인치, 파일경로) - 인치 오름차순 정렬, ALL은 실제 인치들 뒤
    // 미지정(0) 앞에 오도록 정렬(kAllInchValue가 그 사이 값이라 자동으로 그렇게 됨).
    std::vector<std::pair<int, std::string>> Result() const;

protected:
    // § "미지정" 칸 바로 타이핑(2026-09-12) - "미지정이라고 입력칸에 마우스 되면 바로
    // 숫자 넣을 수 있게 해달라, 지금은 전체 선택 후 지우고 입력해야 한다"는 요청. 각
    // 스핀박스의 내부 QLineEdit에 포커스가 들어오는 순간(클릭/탭) 텍스트를 전체
    // 선택해서, 바로 숫자를 치면 "미지정"이 통째로 지워지고 그 숫자로 바뀌게 한다.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QTableWidget* table_ = nullptr;
    QStringList filePaths_;
};

} // namespace ui
