#include "SkiaRenderer.hpp"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkTextBlob.h"
#include "include/effects/SkGradient.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace {
SkSamplingOptions detailSampling() {
    return SkSamplingOptions(SkCubicResampler::CatmullRom());
}
}

std::string SkiaRenderer::formatDetailPrice(const Movie& movie) {
    if (movie.ppv_price == "Free") {
        return "Free";
    }
    return std::string("£") + movie.ppv_price;
}

std::string SkiaRenderer::formatDetailMetadata(const Movie& movie) {
    std::string line = formatDetailPrice(movie);
    line += " · ";
    line += std::to_string(movie.runtime);
    line += " min · RT: ";
    line += std::to_string(movie.rt_score);
    line += "%";
    return line;
}

std::vector<std::string> SkiaRenderer::wrapTextToLines(const std::string& text, float maxWidth, SkFont& font) {
    std::vector<std::string> lines;
    if (text.empty() || maxWidth <= 0.0f) {
        return lines;
    }

    std::istringstream iss(text);
    std::string word;
    std::string currentLine;

    auto flushWordTooLong = [&](std::string& w) {
        if (font.measureText(w.c_str(), w.size(), SkTextEncoding::kUTF8) <= maxWidth) {
            return w;
        }
        return SkiaRenderer::ellipsizeText(w, maxWidth, font);
    };

    while (iss >> word) {
        word = flushWordTooLong(word);
        std::string trial = currentLine.empty() ? word : (currentLine + " " + word);
        float w = font.measureText(trial.c_str(), trial.size(), SkTextEncoding::kUTF8);
        if (w <= maxWidth || currentLine.empty()) {
            currentLine = std::move(trial);
        } else {
            lines.push_back(currentLine);
            currentLine = word;
        }
    }
    if (!currentLine.empty()) {
        lines.push_back(std::move(currentLine));
    }
    return lines;
}

void SkiaRenderer::drawDetailView(SkCanvas* canvas, int width, int height) {
    canvas->clear(SK_ColorBLACK);

    const float s = layoutScale(width, height);
    if (fTypeface) {
        fDetailTitleFont.setSize(kDetailTitleFontSize * s);
        fDetailBodyFont.setSize(kDetailBodyFontSize * s);
        fDetailMetaFont.setSize(kDetailMetaFontSize * s);
    }

    if (fDetailIndex < 0 || fDetailIndex >= static_cast<int>(fMovies.size())) {
        return;
    }

    const Movie& movie = fMovies[fDetailIndex];
    sk_sp<SkImage> poster = posterForDetail(fDetailIndex);

    if (poster) {
        float imgW = static_cast<float>(poster->width());
        float imgH = static_cast<float>(poster->height());
        float scale = std::max(static_cast<float>(width) / imgW, static_cast<float>(height) / imgH);
        float dstW = imgW * scale;
        float dstH = imgH * scale;
        float dstX = (static_cast<float>(width) - dstW) * 0.5f;
        float dstY = (static_cast<float>(height) - dstH) * 0.5f;
        SkRect dstRect = SkRect::MakeXYWH(dstX, dstY, dstW, dstH);

        canvas->save();
        canvas->clipRect(SkRect::MakeWH(static_cast<float>(width), static_cast<float>(height)), true);
        canvas->drawImageRect(poster, dstRect, detailSampling());
        canvas->restore();
    } else {
        canvas->drawColor(SK_ColorDKGRAY);
    }

    const float panelLeft = std::round(static_cast<float>(width) * (1.0f - kDetailPanelFraction));
    const float featherW = std::round(static_cast<float>(width) * kDetailFeatherFraction);
    const float featherLeft = panelLeft - featherW;
    const SkColor panelColor = SkColorSetA(SK_ColorBLACK, 200);

    SkPoint gradPts[2] = {{featherLeft, 0.0f}, {panelLeft, 0.0f}};
    SkColor4f gradColorStops[2] = {
        SkColor4f::FromColor(SK_ColorTRANSPARENT),
        SkColor4f::FromColor(panelColor),
    };
    SkGradient::Colors gradColors(SkSpan<const SkColor4f>(gradColorStops, 2), SkTileMode::kClamp);
    SkGradient gradSpec(gradColors, SkGradient::Interpolation{});
    sk_sp<SkShader> gradShader = SkShaders::LinearGradient(gradPts, gradSpec, nullptr);
    SkPaint featherPaint;
    featherPaint.setShader(gradShader);
    featherPaint.setAntiAlias(false);
    canvas->drawRect(SkRect::MakeLTRB(featherLeft, 0.0f, static_cast<float>(width), static_cast<float>(height)),
        featherPaint);

    const float panelPad = kDetailPanelPadding * s;
    const float innerLeft = panelLeft + panelPad;
    const float innerRight = static_cast<float>(width) - panelPad;
    const float textMaxW = std::max(1.0f, innerRight - innerLeft);

    SkFontMetrics fmTitle;
    fDetailTitleFont.getMetrics(&fmTitle);
    SkFontMetrics fmBody;
    fDetailBodyFont.getMetrics(&fmBody);
    SkFontMetrics fmMeta;
    fDetailMetaFont.getMetrics(&fmMeta);

    const float bodyLineHeight = fmBody.fDescent - fmBody.fAscent + kDetailBodyLineGap * s;

    const float metaBaseline =
        static_cast<float>(height) - panelPad - fmMeta.fDescent;
    const float metaTop = metaBaseline + fmMeta.fAscent;

    std::string titleDisplay = ellipsizeText(movie.title, textMaxW, fDetailTitleFont);
    sk_sp<SkTextBlob> titleBlob =
        SkTextBlob::MakeFromText(titleDisplay.c_str(), titleDisplay.size(), fDetailTitleFont, SkTextEncoding::kUTF8);

    const float titleBaseline = panelPad - fmTitle.fAscent;
    canvas->drawTextBlob(titleBlob, innerLeft, titleBaseline, fDetailTextPaint);

    const float titleBottom = titleBaseline + fmTitle.fDescent;
    const float synopsisTop = titleBottom + kDetailTitleBodyGap * s;
    const float synopsisMaxBottom = metaTop - kDetailBodyMetaGap * s;
    float synopsisAvailable = synopsisMaxBottom - synopsisTop;
    if (synopsisAvailable < bodyLineHeight) {
        synopsisAvailable = bodyLineHeight;
    }

    int maxSynopsisLines = static_cast<int>(std::floor(synopsisAvailable / bodyLineHeight));
    maxSynopsisLines = std::max(1, maxSynopsisLines);

    std::vector<std::string> synopsisLines = wrapTextToLines(movie.synopsis, textMaxW, fDetailBodyFont);
    if (static_cast<int>(synopsisLines.size()) > maxSynopsisLines) {
        synopsisLines.resize(static_cast<size_t>(maxSynopsisLines));
        if (!synopsisLines.empty()) {
            synopsisLines.back() = ellipsizeText(synopsisLines.back(), textMaxW, fDetailBodyFont);
        }
    }

    float bodyY = synopsisTop - fmBody.fAscent;
    canvas->save();
    canvas->clipRect(SkRect::MakeLTRB(innerLeft, synopsisTop, innerRight, synopsisMaxBottom), true);
    for (const std::string& line : synopsisLines) {
        sk_sp<SkTextBlob> blob =
            SkTextBlob::MakeFromText(line.c_str(), line.size(), fDetailBodyFont, SkTextEncoding::kUTF8);
        canvas->drawTextBlob(blob, innerLeft, bodyY, fDetailTextPaint);
        bodyY += bodyLineHeight;
    }
    canvas->restore();

    std::string metaLine = formatDetailMetadata(movie);
    metaLine = ellipsizeText(metaLine, textMaxW, fDetailMetaFont);
    sk_sp<SkTextBlob> metaBlob =
        SkTextBlob::MakeFromText(metaLine.c_str(), metaLine.size(), fDetailMetaFont, SkTextEncoding::kUTF8);
    canvas->drawTextBlob(metaBlob, innerLeft, metaBaseline, fDetailTextPaint);
}
