#pragma once
#include "settings.hpp"

namespace paper {
enum class AnnotationTool { Reading, Pen, Eraser };

class PenController {
    AnnotationTool tool_ = AnnotationTool::Reading;

  public:
    AnnotationTool tool() const { return tool_; }
    bool editing() const { return tool_ != AnnotationTool::Reading; }
    void select(AnnotationTool tool) {
        tool_ = tool_ == tool ? AnnotationTool::Reading : tool;
    }
    void read() { tool_ = AnnotationTool::Reading; }
    static double targetMm(const AppSettings &s, int64_t day) {
        return s.penWidthOverrideDay == day ? s.penScreenWidthMm : s.dailyPenWidthMm;
    }
    static double pageWidth(const AppSettings &s, double zoom, double pixelsPerMm, int64_t day) {
        if (!s.autoPenWidth)
            return s.penWidth;
        return targetMm(s, day) * pixelsPerMm / zoom;
    }
    static void rememberWidth(AppSettings &s, double value, double zoom, double pixelsPerMm, int64_t day) {
        if (s.autoPenWidth) {
            s.penScreenWidthMm = value;
            s.penWidth = value * pixelsPerMm / zoom;
        } else {
            s.penWidth = value;
            s.penScreenWidthMm = std::clamp(value * zoom / pixelsPerMm, AppSettings::minScreenPenWidthMm,
                                            AppSettings::maxScreenPenWidthMm);
        }
        s.penWidthOverrideDay = day;
    }
};
} // namespace paper
