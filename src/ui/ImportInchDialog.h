#pragma once

#include <QDialog>
#include <QStringList>

#include <string>
#include <utility>
#include <vector>

class QTableWidget;

namespace ui {

// STEP 파일을 여러 개 한번에 불러올 때, 파일명에서 자동인식한 인치를 확인/수정하는
// 다이얼로그. 사용자 확인 답변 반영: 폴더/파일명 어딘가에 인치 숫자가 보통 들어있어서
// 자동인식을 시도하되, 실패/애매하면 0(미지정)으로 비워두고 여기서 직접 입력하게 한다.
class ImportInchDialog : public QDialog {
    Q_OBJECT

public:
    ImportInchDialog(const QStringList& filePaths, QWidget* parent = nullptr);

    // Accepted 이후 호출. (인치, 파일경로) - 인치 오름차순 정렬(미지정 0은 맨 뒤).
    std::vector<std::pair<int, std::string>> Result() const;

private:
    QTableWidget* table_ = nullptr;
    QStringList filePaths_;
};

} // namespace ui
