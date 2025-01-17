/**
 * Copyright (C) 2006, 2007, 2010 Apple Inc. All rights reserved.
 *           (C) 2008 Torch Mobile Inc. All rights reserved. (http://www.torchmobile.com/)
 * Copyright (C) 2010 Google Inc. All rights reserved.
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"

#if ENABLE(WML)
#include "core/rendering/wml/RenderWMLTextControlSingleLine.h"

#include "CSSValueKeywords.h"
#include "core/editing/FrameSelection.h"
#include "core/page/Frame.h"
#include "core/platform/PlatformKeyboardEvent.h"
#include "core/platform/graphics/SimpleFontData.h"
#include "core/rendering/HitTestResult.h"
#include "core/rendering/RenderLayer.h"
#include "core/rendering/RenderTheme.h"

#include "base/debug/stack_trace.h"
#include <android/log.h>
#undef XLOG
#define XLOG(...) __android_log_print(ANDROID_LOG_DEBUG, "webview", __VA_ARGS__)

using namespace std;

namespace WebCore {

//using namespace WMLNames;

RenderWMLTextControlSingleLine::RenderWMLTextControlSingleLine(WMLInputElement* element)
    : RenderWMLTextControl(element)
    , m_shouldDrawCapsLockIndicator(false)
    , m_desiredInnerTextLogicalHeight(-1)
{
    ASSERT(element->hasTagName(inputTag));
}

RenderWMLTextControlSingleLine::~RenderWMLTextControlSingleLine()
{
}

inline Element* RenderWMLTextControlSingleLine::innerSpinButtonElement() const
{
    //return inputElement()->innerSpinButtonElement();
    return 0;
}

RenderStyle* RenderWMLTextControlSingleLine::textBaseStyle() const
{
    Element* innerBlock = innerBlockElement();
    return innerBlock ? innerBlock->renderer()->style() : style();
}

void RenderWMLTextControlSingleLine::paint(PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    RenderWMLTextControl::paint(paintInfo, paintOffset);

    if (paintInfo.phase == PaintPhaseBlockBackground && m_shouldDrawCapsLockIndicator) {
        LayoutRect contentsRect = contentBoxRect();

        // Center in the block progression direction.
        if (isHorizontalWritingMode())
            contentsRect.setY((height() - contentsRect.height()) / 2);
        else
            contentsRect.setX((width() - contentsRect.width()) / 2);

        // Convert the rect into the coords used for painting the content
        contentsRect.moveBy(paintOffset + location());
        theme()->paintCapsLockIndicator(this, paintInfo, pixelSnappedIntRect(contentsRect));
    }
}

LayoutUnit RenderWMLTextControlSingleLine::computeLogicalHeightLimit() const
{
    return containerElement() ? contentLogicalHeight() : logicalHeight();
}

// 'end' must be an ancestor of 'start.'
static void setNeedsLayoutInRange(RenderObject* start, RenderObject* end)
{
    ASSERT(start);
    ASSERT(start != end);
    for (RenderObject* renderer = start; renderer != end; renderer = renderer->parent())
        renderer->setNeedsLayout(MarkOnlyThis);
}

void RenderWMLTextControlSingleLine::layout()
{
    StackStats::LayoutCheckPoint layoutCheckPoint;

    // FIXME: We should remove the height-related hacks in layout() and
    // styleDidChange(). We need them because
    // - Center the inner elements vertically if the input height is taller than
    //   the intrinsic height of the inner elements.
    // - Shrink the inner elment heights if the input height is samller than the
    //   intrinsic heights of the inner elements.

    // We don't honor paddings and borders for textfields without decorations
    // and type=search if the text height is taller than the contentHeight()
    // because of compability.

    RenderBox* innerTextRenderer = innerTextElement()->renderBox();
    RenderBox* innerBlockRenderer = innerBlockElement() ? innerBlockElement()->renderBox() : 0;

    // To ensure consistency between layouts, we need to reset any conditionally overriden height.
    if (innerTextRenderer && !innerTextRenderer->style()->logicalHeight().isAuto()) {
        innerTextRenderer->style()->setLogicalHeight(Length(Auto));
        setNeedsLayoutInRange(innerTextRenderer, this);
    }
    if (innerBlockRenderer && !innerBlockRenderer->style()->logicalHeight().isAuto()) {
        innerBlockRenderer->style()->setLogicalHeight(Length(Auto));
        setNeedsLayoutInRange(innerBlockRenderer, this);
    }

    RenderBlock::layoutBlock(false);

    Element* container = containerElement();
    RenderBox* containerRenderer = container ? container->renderBox() : 0;

    // Set the text block height
    LayoutUnit desiredLogicalHeight = textBlockLogicalHeight();
    LayoutUnit logicalHeightLimit = computeLogicalHeightLimit();
    if (innerTextRenderer && innerTextRenderer->logicalHeight() > logicalHeightLimit) {
        if (desiredLogicalHeight != innerTextRenderer->logicalHeight())
            setNeedsLayout(MarkOnlyThis);

        m_desiredInnerTextLogicalHeight = desiredLogicalHeight;

        innerTextRenderer->style()->setLogicalHeight(Length(desiredLogicalHeight, Fixed));
        innerTextRenderer->setNeedsLayout(MarkOnlyThis);
        if (innerBlockRenderer) {
            innerBlockRenderer->style()->setLogicalHeight(Length(desiredLogicalHeight, Fixed));
            innerBlockRenderer->setNeedsLayout(MarkOnlyThis);
        }
    }
    // The container might be taller because of decoration elements.
    if (containerRenderer) {
        containerRenderer->layoutIfNeeded();
        LayoutUnit containerLogicalHeight = containerRenderer->logicalHeight();
        if (containerLogicalHeight > logicalHeightLimit) {
            containerRenderer->style()->setLogicalHeight(Length(logicalHeightLimit, Fixed));
            setNeedsLayout(MarkOnlyThis);
        } else if (containerRenderer->logicalHeight() < contentLogicalHeight()) {
            containerRenderer->style()->setLogicalHeight(Length(contentLogicalHeight(), Fixed));
            setNeedsLayout(MarkOnlyThis);
        } else
            containerRenderer->style()->setLogicalHeight(Length(containerLogicalHeight, Fixed));
    }

    // If we need another layout pass, we have changed one of children's height so we need to relayout them.
    if (needsLayout())
        RenderBlock::layoutBlock(true);

    // Center the child block in the block progression direction (vertical centering for horizontal text fields).
    if (!container && innerTextRenderer && innerTextRenderer->height() != contentLogicalHeight()) {
        LayoutUnit logicalHeightDiff = innerTextRenderer->logicalHeight() - contentLogicalHeight();
        innerTextRenderer->setLogicalTop(innerTextRenderer->logicalTop() - (logicalHeightDiff / 2 + layoutMod(logicalHeightDiff, 2)));
    } else
        centerContainerIfNeeded(containerRenderer);

    // Ignores the paddings for the inner spin button.
    if (RenderBox* innerSpinBox = innerSpinButtonElement() ? innerSpinButtonElement()->renderBox() : 0) {
        RenderBox* parentBox = innerSpinBox->parentBox();
        if (containerRenderer && !containerRenderer->style()->isLeftToRightDirection())
            innerSpinBox->setLogicalLocation(LayoutPoint(-paddingLogicalLeft(), -paddingBefore()));
        else
            innerSpinBox->setLogicalLocation(LayoutPoint(parentBox->logicalWidth() - innerSpinBox->logicalWidth() + paddingLogicalRight(), -paddingBefore()));
        innerSpinBox->setLogicalHeight(logicalHeight() - borderBefore() - borderAfter());
    }

    //Element* placeholderElement = inputElement()->placeholderElement();
    Element* placeholderElement = 0;
    if (RenderBox* placeholderBox = placeholderElement ? placeholderElement->renderBox() : 0) {
        LayoutSize innerTextSize;
        if (innerTextRenderer)
            innerTextSize = innerTextRenderer->size();
        placeholderBox->style()->setWidth(Length(innerTextSize.width() - placeholderBox->borderAndPaddingWidth(), Fixed));
        placeholderBox->style()->setHeight(Length(innerTextSize.height() - placeholderBox->borderAndPaddingHeight(), Fixed));
        bool neededLayout = placeholderBox->needsLayout();
        bool placeholderBoxHadLayout = placeholderBox->everHadLayout();
        placeholderBox->layoutIfNeeded();
        LayoutPoint textOffset;
        if (innerTextRenderer)
            textOffset = innerTextRenderer->location();
        if (innerBlockElement() && innerBlockElement()->renderBox())
            textOffset += toLayoutSize(innerBlockElement()->renderBox()->location());
        if (containerRenderer)
            textOffset += toLayoutSize(containerRenderer->location());
        placeholderBox->setLocation(textOffset);

        if (!placeholderBoxHadLayout && placeholderBox->checkForRepaintDuringLayout()) {
            // This assumes a shadow tree without floats. If floats are added, the
            // logic should be shared with RenderBlock::layoutBlockChild.
            placeholderBox->repaint();
        }
        // The placeholder gets layout last, after the parent text control and its other children,
        // so in order to get the correct overflow from the placeholder we need to recompute it now.
        if (neededLayout)
            computeOverflow(clientLogicalBottom());
    }
}

bool RenderWMLTextControlSingleLine::nodeAtPoint(const HitTestRequest& request, HitTestResult& result, const HitTestLocation& locationInContainer, const LayoutPoint& accumulatedOffset, HitTestAction hitTestAction)
{
    if (!RenderWMLTextControl::nodeAtPoint(request, result, locationInContainer, accumulatedOffset, hitTestAction))
        return false;

    // Say that we hit the inner text element if
    //  - we hit a node inside the inner text element,
    //  - we hit the <input> element (e.g. we're over the border or padding), or
    //  - we hit regions not in any decoration buttons.
    Element* container = containerElement();
    if (result.innerNode()->isDescendantOf(innerTextElement()) || result.innerNode() == node() || (container && container == result.innerNode())) {
        LayoutPoint pointInParent = locationInContainer.point();
        if (container && innerBlockElement()) {
            if (innerBlockElement()->renderBox())
                pointInParent -= toLayoutSize(innerBlockElement()->renderBox()->location());
            if (container->renderBox())
                pointInParent -= toLayoutSize(container->renderBox()->location());
        }
        hitInnerTextElement(result, pointInParent, accumulatedOffset);
    }
    return true;
}

void RenderWMLTextControlSingleLine::styleDidChange(StyleDifference diff, const RenderStyle* oldStyle)
{
    m_desiredInnerTextLogicalHeight = -1;
    RenderWMLTextControl::styleDidChange(diff, oldStyle);

    // We may have set the width and the height in the old style in layout().
    // Reset them now to avoid getting a spurious layout hint.
    Element* innerBlock = innerBlockElement();
    if (RenderObject* innerBlockRenderer = innerBlock ? innerBlock->renderer() : 0) {
        innerBlockRenderer->style()->setHeight(Length());
        innerBlockRenderer->style()->setWidth(Length());
    }
    Element* container = containerElement();
    if (RenderObject* containerRenderer = container ? container->renderer() : 0) {
        containerRenderer->style()->setHeight(Length());
        containerRenderer->style()->setWidth(Length());
    }
    RenderObject* innerTextRenderer = innerTextElement()->renderer();
    if (innerTextRenderer && diff == StyleDifferenceLayout)
        innerTextRenderer->setNeedsLayout(MarkContainingBlockChain);
    if (Element* placeholder = inputElement()->placeholderElement())
        placeholder->setInlineStyleProperty(CSSPropertyTextOverflow, textShouldBeTruncated() ? CSSValueEllipsis : CSSValueClip);
    setHasOverflowClip(false);
}

void RenderWMLTextControlSingleLine::capsLockStateMayHaveChanged()
{
    if (!node() || !document())
        return;

    // Only draw the caps lock indicator if these things are true:
    // 1) The field is a password field
    // 2) The frame is active
    // 3) The element is focused
    // 4) The caps lock is on
    bool shouldDrawCapsLockIndicator = false;

    if (Frame* frame = document()->frame())
        shouldDrawCapsLockIndicator = inputElement()->isPasswordField() && frame->selection()->isFocusedAndActive() && document()->focusedElement() == node() && PlatformKeyboardEvent::currentCapsLockState();

    XLOG("[RenderWMLTextControlSingleLine::capsLockStateMayHaveChanged] [%p][%d][%d] should=[%d][%d]"
                , this, getpid(), gettid(), shouldDrawCapsLockIndicator, m_shouldDrawCapsLockIndicator);
    if (shouldDrawCapsLockIndicator != m_shouldDrawCapsLockIndicator) {
        m_shouldDrawCapsLockIndicator = shouldDrawCapsLockIndicator;
        repaint();
    }
}

bool RenderWMLTextControlSingleLine::hasControlClip() const
{
    // Apply control clip for text fields with decorations.
    return !!containerElement();
}

LayoutRect RenderWMLTextControlSingleLine::controlClipRect(const LayoutPoint& additionalOffset) const
{
    ASSERT(hasControlClip());
    LayoutRect clipRect = contentBoxRect();
    if (containerElement()->renderBox())
        clipRect = unionRect(clipRect, containerElement()->renderBox()->frameRect());
    clipRect.moveBy(additionalOffset);
    return clipRect;
}

float RenderWMLTextControlSingleLine::getAvgCharWidth(AtomicString family)
{
    // Since Lucida Grande is the default font, we want this to match the width
    // of MS Shell Dlg, the default font for textareas in Firefox, Safari Win and
    // IE for some encodings (in IE, the default font is encoding specific).
    // 901 is the avgCharWidth value in the OS/2 table for MS Shell Dlg.
    if (family == "Lucida Grande")
        return scaleEmToUnits(901);

    return RenderWMLTextControl::getAvgCharWidth(family);
}

LayoutUnit RenderWMLTextControlSingleLine::preferredContentLogicalWidth(float charWidth) const
{
    int factor = 0;
    //bool includesDecoration = inputElement()->sizeShouldIncludeDecoration(factor);
    bool includesDecoration = false;
    if (factor <= 0)
        factor = 20;

    LayoutUnit result = LayoutUnit::fromFloatCeil(charWidth * factor);

    float maxCharWidth = 0.f;
    AtomicString family = style()->font().family().family();
    // Since Lucida Grande is the default font, we want this to match the width
    // of MS Shell Dlg, the default font for textareas in Firefox, Safari Win and
    // IE for some encodings (in IE, the default font is encoding specific).
    // 4027 is the (xMax - xMin) value in the "head" font table for MS Shell Dlg.
    if (family == "Lucida Grande")
        maxCharWidth = scaleEmToUnits(4027);
    else if (hasValidAvgCharWidth(family))
        maxCharWidth = roundf(style()->font().primaryFont()->maxCharWidth());

    // For text inputs, IE adds some extra width.
    if (maxCharWidth > 0.f)
        result += maxCharWidth - charWidth;

    if (includesDecoration) {
        Element* spinButton = innerSpinButtonElement();
        if (RenderBox* spinRenderer = spinButton ? spinButton->renderBox() : 0) {
            result += spinRenderer->borderAndPaddingLogicalWidth();
            // Since the width of spinRenderer is not calculated yet, spinRenderer->logicalWidth() returns 0.
            // So computedStyle()->logicalWidth() is used instead.
            result += spinButton->computedStyle()->logicalWidth().value();
        }
    }

    return result;
}

LayoutUnit RenderWMLTextControlSingleLine::computeControlLogicalHeight(LayoutUnit lineHeight, LayoutUnit nonContentHeight) const
{
    return lineHeight + nonContentHeight;
}

void RenderWMLTextControlSingleLine::updateFromElement()
{
    RenderWMLTextControl::updateFromElement();
}

PassRefPtr<RenderStyle> RenderWMLTextControlSingleLine::createInnerTextStyle(const RenderStyle* startStyle) const
{
    RefPtr<RenderStyle> textBlockStyle = RenderStyle::create();
    textBlockStyle->inheritFrom(startStyle);
    adjustInnerTextStyle(textBlockStyle.get());

    textBlockStyle->setWhiteSpace(PRE);
    textBlockStyle->setOverflowWrap(NormalOverflowWrap);
    textBlockStyle->setOverflowX(OHIDDEN);
    textBlockStyle->setOverflowY(OHIDDEN);
    textBlockStyle->setTextOverflow(textShouldBeTruncated() ? TextOverflowEllipsis : TextOverflowClip);

    if (m_desiredInnerTextLogicalHeight >= 0)
        textBlockStyle->setLogicalHeight(Length(m_desiredInnerTextLogicalHeight, Fixed));
    // Do not allow line-height to be smaller than our default.
    if (textBlockStyle->fontMetrics().lineSpacing() > lineHeight(true, HorizontalLine, PositionOfInteriorLineBoxes))
        textBlockStyle->setLineHeight(RenderStyle::initialLineHeight());

    textBlockStyle->setDisplay(BLOCK);

    return textBlockStyle.release();
}

PassRefPtr<RenderStyle> RenderWMLTextControlSingleLine::createInnerBlockStyle(const RenderStyle* startStyle) const
{
    RefPtr<RenderStyle> innerBlockStyle = RenderStyle::create();
    innerBlockStyle->inheritFrom(startStyle);

    innerBlockStyle->setFlexGrow(1);
    // min-width: 0; is needed for correct shrinking.
    // FIXME: Remove this line when https://bugs.webkit.org/show_bug.cgi?id=111790 is fixed.
    innerBlockStyle->setMinWidth(Length(0, Fixed));
    innerBlockStyle->setDisplay(BLOCK);
    innerBlockStyle->setDirection(LTR);

    // We don't want the shadow dom to be editable, so we set this block to read-only in case the input itself is editable.
    innerBlockStyle->setUserModify(READ_ONLY);

    return innerBlockStyle.release();
}

bool RenderWMLTextControlSingleLine::textShouldBeTruncated() const
{
    return document()->focusedElement() != node() && style()->textOverflow() == TextOverflowEllipsis;
}

void RenderWMLTextControlSingleLine::autoscroll(const IntPoint& position)
{
    RenderBox* renderer = innerTextElement()->renderBox();
    if (!renderer)
        return;
    RenderLayer* layer = renderer->layer();
    if (layer)
        layer->autoscroll(position);
}

int RenderWMLTextControlSingleLine::scrollWidth() const
{
    if (innerTextElement())
        return innerTextElement()->scrollWidth();
    return RenderBlock::scrollWidth();
}

int RenderWMLTextControlSingleLine::scrollHeight() const
{
    if (innerTextElement())
        return innerTextElement()->scrollHeight();
    return RenderBlock::scrollHeight();
}

int RenderWMLTextControlSingleLine::scrollLeft() const
{
    if (innerTextElement())
        return innerTextElement()->scrollLeft();
    return RenderBlock::scrollLeft();
}

int RenderWMLTextControlSingleLine::scrollTop() const
{
    if (innerTextElement())
        return innerTextElement()->scrollTop();
    return RenderBlock::scrollTop();
}

void RenderWMLTextControlSingleLine::setScrollLeft(int newLeft)
{
    if (innerTextElement())
        innerTextElement()->setScrollLeft(newLeft);
}

void RenderWMLTextControlSingleLine::setScrollTop(int newTop)
{
    if (innerTextElement())
        innerTextElement()->setScrollTop(newTop);
}

bool RenderWMLTextControlSingleLine::scroll(ScrollDirection direction, ScrollGranularity granularity, float multiplier, Node** stopNode)
{
    RenderBox* renderer = innerTextElement()->renderBox();
    if (!renderer)
        return false;
    RenderLayer* layer = renderer->layer();
    if (layer && layer->scroll(direction, granularity, multiplier))
        return true;
    return RenderBlock::scroll(direction, granularity, multiplier, stopNode);
}

bool RenderWMLTextControlSingleLine::logicalScroll(ScrollLogicalDirection direction, ScrollGranularity granularity, float multiplier, Node** stopNode)
{
    RenderLayer* layer = innerTextElement()->renderBox()->layer();
    if (layer && layer->scroll(logicalToPhysical(direction, style()->isHorizontalWritingMode(), style()->isFlippedBlocksWritingMode()), granularity, multiplier))
        return true;
    return RenderBlock::logicalScroll(direction, granularity, multiplier, stopNode);
}

WMLInputElement* RenderWMLTextControlSingleLine::inputElement() const
{
    return toWMLInputElement(node());
}

}

#endif
