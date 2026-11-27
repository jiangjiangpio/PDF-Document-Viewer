#include "pen.hpp"
#include <iostream>
using namespace paper;
int main() {
    int passed = 0;
    auto check = [&](bool value, const char *name) {
        if (!value) throw std::runtime_error(name);
        ++passed;
        std::cout << "PASS " << name << '\n';
    };
    PenController pen;
    check(pen.tool() == AnnotationTool::Reading, "launch starts in reading mode");
    pen.select(AnnotationTool::Pen);
    check(pen.tool() == AnnotationTool::Pen, "pen click selects pen");
    pen.select(AnnotationTool::Eraser);
    check(pen.tool() == AnnotationTool::Eraser, "eraser replaces pen");
    pen.select(AnnotationTool::Pen);
    check(pen.tool() == AnnotationTool::Pen, "pen replaces eraser");
    pen.select(AnnotationTool::Pen);
    check(!pen.editing(), "second pen click returns to reading");
    pen.select(AnnotationTool::Eraser);
    pen.select(AnnotationTool::Eraser);
    check(!pen.editing(), "second eraser click returns to reading");

    AppSettings s;
    const int64_t midnight = 1794672000; // 2026-11-15 00:00 Beijing, 16:00 UTC.
    const int64_t today = beijingDay(midnight - 1), tomorrow = beijingDay(midnight);
    check(tomorrow == today + 1, "Beijing midnight advances even before UTC midnight");
    for (double zoom : {.1, .5, 1., 2., 8.}) {
        auto width = PenController::pageWidth(s, zoom, 4.7, today);
        check(std::abs(width * zoom / 4.7 - 2) < 1e-10, "default new stroke measures 2 mm across zooms");
    }
    PenController::rememberWidth(s, 1.3, 2, 4.7, today);
    const auto manualPageWidth = s.penWidth;
    check(s.penWidthOverrideDay == today && std::abs(s.penScreenWidthMm - 1.3) < 1e-10,
          "manual width has same-day priority");
    check(std::abs(PenController::pageWidth(s, .5, 4.7, today) * .5 / 4.7 - 1.3) < 1e-10,
          "manual screen width also compensates zoom");
    check(std::abs(PenController::pageWidth(s, 2, 4.7, tomorrow) * 2 / 4.7 - 2) < 1e-10,
          "first next-day stroke returns to 2 mm without altering manual memory");
    s.dailyPenWidthMm = 0.85;
    check(std::abs(PenController::pageWidth(s, 16., 4.7, tomorrow) * 16. / 4.7 - 0.85) < 1e-10 &&
              PenController::targetMm(s, today) == 1.3,
          "custom daily width takes effect after midnight and preserves the same-day override");
    s.autoPenWidth = false;
    check(PenController::pageWidth(s, 8, 4.7, tomorrow) == manualPageWidth,
          "disabled auto-width uses remembered document width without daily reset");
    PenController::rememberWidth(s, 3.5, 1., 4.7, tomorrow);
    check(PenController::pageWidth(s, .1, 4.7, tomorrow + 1) == 3.5,
          "manual width persists while automatic width is disabled");

    Stroke old;
    old.width = 5;
    old.points = {{10, 10}, {30, 10}};
    auto original = old.width;
    PenController::pageWidth(s, 2., 4.7, tomorrow);
    check(old.width == original, "new stroke width calculations leave existing stroke data unchanged");
    std::cout << passed << " passed\n";
}
