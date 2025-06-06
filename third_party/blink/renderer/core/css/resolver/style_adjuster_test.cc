// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/test/scoped_feature_list.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/frame/event_handler_registry.h"
#include "third_party/blink/renderer/core/testing/core_unit_test_helper.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"
#include "ui/base/ui_base_features.h"

namespace blink {

class StyleAdjusterTest : public RenderingTest {
 public:
  StyleAdjusterTest()
      : RenderingTest(MakeGarbageCollected<SingleChildLocalFrameClient>()) {}
};

TEST_F(StyleAdjusterTest, TouchActionPropagatedAcrossIframes) {
  GetDocument().SetBaseURLOverride(KURL("http://test.com"));
  SetBodyInnerHTML(R"HTML(
    <style>body { margin: 0; } iframe { display: block; } </style>
    <iframe id='owner' src='http://test.com' width='500' height='500'
    style='touch-action: none'>
    </iframe>
  )HTML");
  SetChildFrameHTML(R"HTML(
    <style>body { margin: 0; } #target { width: 200px; height: 200px; }
    </style>
    <div id='target' style='touch-action: pinch-zoom'></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* target = ChildDocument().getElementById(AtomicString("target"));
  EXPECT_EQ(TouchAction::kNone,
            target->GetComputedStyle()->EffectiveTouchAction());

  Element* owner = GetDocument().getElementById(AtomicString("owner"));
  owner->setAttribute(html_names::kStyleAttr,
                      AtomicString("touch-action: auto"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(TouchAction::kPinchZoom,
            target->GetComputedStyle()->EffectiveTouchAction());
}

TEST_F(StyleAdjusterTest, TouchActionPanningReEnabledByScrollers) {
  GetDocument().SetBaseURLOverride(KURL("http://test.com"));
  SetBodyInnerHTML(R"HTML(
    <style>#ancestor { margin: 0; touch-action: pinch-zoom; }
    #scroller { overflow: scroll; width: 100px; height: 100px; }
    #target { width: 200px; height: 200px; } </style>
    <div id='ancestor'><div id='scroller'><div id='target'>
    </div></div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* target = GetDocument().getElementById(AtomicString("target"));
  // TODO(crbug.com/382525574): Launch or clean up kHandwriting.
  // This check also tests that TouchAction::kInternalHandwritingPanningRules
  // is set, as it is included in kManipulation.
  EXPECT_EQ((TouchAction::kManipulation & ~TouchAction::kInternalHandwriting) |
                TouchAction::kInternalPanXScrolls |
                TouchAction::kInternalNotWritable,
            target->GetComputedStyle()->EffectiveTouchAction());
}

TEST_F(StyleAdjusterTest, TouchActionPropagatedWhenAncestorStyleChanges) {
  GetDocument().SetBaseURLOverride(KURL("http://test.com"));
  SetBodyInnerHTML(R"HTML(
    <style>#ancestor { margin: 0; touch-action: pan-x; }
    #potential-scroller { width: 100px; height: 100px; overflow: hidden; }
    #target { width: 200px; height: 200px; }</style>
    <div id='ancestor'><div id='potential-scroller'><div id='target'>
    </div></div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* target = GetDocument().getElementById(AtomicString("target"));
  EXPECT_EQ(TouchAction::kPanX | TouchAction::kInternalPanXScrolls |
                TouchAction::kInternalNotWritable,
            target->GetComputedStyle()->EffectiveTouchAction());

  Element* ancestor = GetDocument().getElementById(AtomicString("ancestor"));
  ancestor->setAttribute(html_names::kStyleAttr,
                         AtomicString("touch-action: pan-y"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(TouchAction::kPanY | TouchAction::kInternalNotWritable,
            target->GetComputedStyle()->EffectiveTouchAction());

  Element* potential_scroller =
      GetDocument().getElementById(AtomicString("potential-scroller"));
  potential_scroller->setAttribute(html_names::kStyleAttr,
                                   AtomicString("overflow: scroll"));
  UpdateAllLifecyclePhasesForTest();
  // TODO(crbug.com/382525574): Launch or clean up kHandwriting.
  EXPECT_EQ(TouchAction::kPan | TouchAction::kInternalPanXScrolls |
                TouchAction::kInternalNotWritable |
                TouchAction::kInternalHandwritingPanningRules,
            target->GetComputedStyle()->EffectiveTouchAction());
}

TEST_F(StyleAdjusterTest, TouchActionRestrictedByLowerAncestor) {
  GetDocument().SetBaseURLOverride(KURL("http://test.com"));
  SetBodyInnerHTML(R"HTML(
    <div id='ancestor' style='touch-action: pan'>
    <div id='parent' style='touch-action: pan-right pan-y'>
    <div id='target' style='touch-action: pan-x'>
    </div></div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* target = GetDocument().getElementById(AtomicString("target"));
  EXPECT_EQ(TouchAction::kPanRight | TouchAction::kInternalPanXScrolls |
                TouchAction::kInternalNotWritable,
            target->GetComputedStyle()->EffectiveTouchAction());

  Element* parent = GetDocument().getElementById(AtomicString("parent"));
  parent->setAttribute(html_names::kStyleAttr,
                       AtomicString("touch-action: auto"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(TouchAction::kPanX | TouchAction::kInternalPanXScrolls |
                TouchAction::kInternalNotWritable,
            target->GetComputedStyle()->EffectiveTouchAction());
}

TEST_F(StyleAdjusterTest, TouchActionContentEditableArea) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeatures({::features::kSwipeToMoveCursor}, {});
  if (!::features::IsSwipeToMoveCursorEnabled()) {
    return;
  }

  GetDocument().SetBaseURLOverride(KURL("http://test.com"));
  SetBodyInnerHTML(R"HTML(
    <div id='editable1' contenteditable='false'></div>
    <input type="text" id='input1' disabled>
    <textarea id="textarea1" readonly></textarea>
    <div id='editable2' contenteditable='true'></div>
    <input type="text" id='input2'>
    <textarea id="textarea2"></textarea>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ(TouchAction::kAuto, GetDocument()
                                    .getElementById(AtomicString("editable1"))
                                    ->GetComputedStyle()
                                    ->EffectiveTouchAction());
  EXPECT_EQ(TouchAction::kAuto, GetDocument()
                                    .getElementById(AtomicString("input1"))
                                    ->GetComputedStyle()
                                    ->EffectiveTouchAction());
  EXPECT_EQ(TouchAction::kAuto, GetDocument()
                                    .getElementById(AtomicString("textarea1"))
                                    ->GetComputedStyle()
                                    ->EffectiveTouchAction());
  EXPECT_EQ(TouchAction::kAuto & ~TouchAction::kInternalPanXScrolls,
            GetDocument()
                .getElementById(AtomicString("editable2"))
                ->GetComputedStyle()
                ->EffectiveTouchAction());
  EXPECT_EQ(TouchAction::kAuto & ~TouchAction::kInternalPanXScrolls,
            GetDocument()
                .getElementById(AtomicString("input2"))
                ->GetComputedStyle()
                ->EffectiveTouchAction());
  EXPECT_EQ(TouchAction::kAuto & ~TouchAction::kInternalPanXScrolls,
            GetDocument()
                .getElementById(AtomicString("textarea2"))
                ->GetComputedStyle()
                ->EffectiveTouchAction());

  Element* target = GetDocument().getElementById(AtomicString("editable1"));
  target->setAttribute(html_names::kContenteditableAttr, keywords::kTrue);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(TouchAction::kAuto & ~TouchAction::kInternalPanXScrolls,
            target->GetComputedStyle()->EffectiveTouchAction());
}

TEST_F(StyleAdjusterTest, TouchActionNoPanXScrollsWhenNoPanX) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeatures({::features::kSwipeToMoveCursor}, {});
  if (!::features::IsSwipeToMoveCursorEnabled()) {
    return;
  }

  GetDocument().SetBaseURLOverride(KURL("http://test.com"));
  SetBodyInnerHTML(R"HTML(
    <div id='target' contenteditable='false' style='touch-action: pan-y'></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* target = GetDocument().getElementById(AtomicString("target"));
  EXPECT_EQ(TouchAction::kPanY | TouchAction::kInternalNotWritable,
            target->GetComputedStyle()->EffectiveTouchAction());

  target->setAttribute(html_names::kContenteditableAttr, keywords::kTrue);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(TouchAction::kPanY | TouchAction::kInternalNotWritable,
            target->GetComputedStyle()->EffectiveTouchAction());
}

TEST_F(StyleAdjusterTest, TouchActionNotWritableReEnabledByScrollers) {
  base::test::ScopedFeatureList feature_list;
  ScopedStylusHandwritingForTest stylus_handwriting(true);

  GetDocument().SetBaseURLOverride(KURL("http://test.com"));
  SetBodyInnerHTML(R"HTML(
    <style>#ancestor { margin: 0; touch-action: none; }
    #scroller { overflow: auto; width: 100px; height: 100px; }
    #target { width: 200px; height: 200px; } </style>
    <div id='ancestor'><div id='scroller'><div id='target'>
    </div></div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* target = GetDocument().getElementById(AtomicString("target"));
  EXPECT_TRUE((target->GetComputedStyle()->EffectiveTouchAction() &
               TouchAction::kInternalNotWritable) != TouchAction::kNone);
}

TEST_F(StyleAdjusterTest, TouchActionWritableArea) {
  base::test::ScopedFeatureList feature_list;
  ScopedStylusHandwritingForTest stylus_handwriting(true);

  GetDocument().SetBaseURLOverride(KURL("http://test.com"));
  SetBodyInnerHTML(R"HTML(
    <div id='editable1' contenteditable='false'></div>
    <input type="text" id='input1' disabled>
    <input type="password" id='password1' disabled>
    <textarea id="textarea1" readonly></textarea>
    <div id='editable2' contenteditable='true'></div>
    <input type="text" id='input2'>
    <input type="password" id='password2'>
    <textarea id="textarea2"></textarea>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  EXPECT_EQ(TouchAction::kAuto, GetDocument()
                                    .getElementById(AtomicString("editable1"))
                                    ->GetComputedStyle()
                                    ->EffectiveTouchAction());
  EXPECT_EQ(TouchAction::kAuto, GetDocument()
                                    .getElementById(AtomicString("input1"))
                                    ->GetComputedStyle()
                                    ->EffectiveTouchAction());
  EXPECT_EQ(TouchAction::kAuto, GetDocument()
                                    .getElementById(AtomicString("password1"))
                                    ->GetComputedStyle()
                                    ->EffectiveTouchAction());
  EXPECT_EQ(TouchAction::kAuto, GetDocument()
                                    .getElementById(AtomicString("textarea1"))
                                    ->GetComputedStyle()
                                    ->EffectiveTouchAction());

  TouchAction expected_input_action =
      (TouchAction::kAuto & ~TouchAction::kInternalNotWritable);
  TouchAction expected_pwd_action = TouchAction::kAuto;
  if (::features::IsSwipeToMoveCursorEnabled()) {
    expected_input_action &= ~TouchAction::kInternalPanXScrolls;
    expected_pwd_action &= ~TouchAction::kInternalPanXScrolls;
  }

  EXPECT_EQ(expected_input_action,
            GetDocument()
                .getElementById(AtomicString("editable2"))
                ->GetComputedStyle()
                ->EffectiveTouchAction());
  EXPECT_EQ(expected_input_action, GetDocument()
                                       .getElementById(AtomicString("input2"))
                                       ->GetComputedStyle()
                                       ->EffectiveTouchAction());
  EXPECT_EQ(expected_pwd_action, GetDocument()
                                     .getElementById(AtomicString("password2"))
                                     ->GetComputedStyle()
                                     ->EffectiveTouchAction());
  EXPECT_EQ(expected_input_action,
            GetDocument()
                .getElementById(AtomicString("textarea2"))
                ->GetComputedStyle()
                ->EffectiveTouchAction());

  Element* target = GetDocument().getElementById(AtomicString("editable1"));
  target->setAttribute(html_names::kContenteditableAttr, keywords::kTrue);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(expected_input_action,
            target->GetComputedStyle()->EffectiveTouchAction());
}

// Non-writable elements shouldn't signal that they would lose handwriting
// capabilities.
TEST_F(StyleAdjusterTest, TouchActionHandwriting_NotWritable) {
  SetBodyInnerHTML(R"HTML(
    <div style="width: 100px; height: 100px;"/>
  )HTML");
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::kNonNoneTouchActionWouldLoseEditableHandwriting));
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::
          kNonNoneTouchActionWouldLoseEditableHandwritingRestoredByScroller));
}

// Non-writable elements shouldn't signal that they would lose handwriting
// capabilities even if `touch-action` is specified.
TEST_F(StyleAdjusterTest, TouchActionHandwriting_NotWritableTouchAction) {
  SetBodyInnerHTML(R"HTML(
    <div style="touch-action: pan-x; width: 100px; height: 100px;"/>
  )HTML");
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::kNonNoneTouchActionWouldLoseEditableHandwriting));
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::
          kNonNoneTouchActionWouldLoseEditableHandwritingRestoredByScroller));
}

// Writable elements shouldn't signal that they would lose handwriting
// capabilities if `touch-action` is not specified.
TEST_F(StyleAdjusterTest, TouchActionHandwriting_NoTouchAction) {
  SetBodyInnerHTML(R"HTML(
    <input type="text" style="width: 100px; height: 100px;"></input>
  )HTML");
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::kNonNoneTouchActionWouldLoseEditableHandwriting));
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::
          kNonNoneTouchActionWouldLoseEditableHandwritingRestoredByScroller));
}

// Writable elements shouldn't signal that they would lose handwriting
// capabilities if `touch-action: auto` is specified.
TEST_F(StyleAdjusterTest, TouchActionHandwriting_Auto) {
  SetBodyInnerHTML(R"HTML(
    <input style="touch-action:auto; height:100px; width:100px" type="text"/>
  )HTML");
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::kNonNoneTouchActionWouldLoseEditableHandwriting));
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::
          kNonNoneTouchActionWouldLoseEditableHandwritingRestoredByScroller));
}

// Writable elements shouldn't signal that they would lose handwriting
// capabilities if `touch-action: manipulation` is specified.
TEST_F(StyleAdjusterTest, TouchActionHandwriting_Manipulation) {
  SetBodyInnerHTML(R"HTML(
    <input style="touch-action:manipulation; height:100px; width:100px"
      type="text"/>
  )HTML");
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::kNonNoneTouchActionWouldLoseEditableHandwriting));
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::
          kNonNoneTouchActionWouldLoseEditableHandwritingRestoredByScroller));
}

// Writable elements shouldn't signal that they would lose handwriting
// capabilities if `touch-action: none` is specified, as handwriting would be
// intentionally disabled.
TEST_F(StyleAdjusterTest, TouchActionHandwriting_TouchActionNone) {
  SetBodyInnerHTML(R"HTML(
    <input style="touch-action:none; height:100px; width:100px"
      type="text"/>
  )HTML");
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::kNonNoneTouchActionWouldLoseEditableHandwriting));
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::
          kNonNoneTouchActionWouldLoseEditableHandwritingRestoredByScroller));
}

// Writable elements should signal that they would lose handwriting
// capabilities if `touch-action` is specified with a value other than
// `manipulation` or `auto`.
TEST_F(StyleAdjusterTest, TouchActionHandwriting_TouchActionDeclared) {
  SetBodyInnerHTML(R"HTML(
    <input style="touch-action:pan-x pan-y pinch-zoom; height:100px; width:100px"
      type="text"/>
  )HTML");
  EXPECT_TRUE(GetDocument().IsUseCounted(
      WebFeature::kNonNoneTouchActionWouldLoseEditableHandwriting));
  EXPECT_TRUE(GetDocument().IsUseCounted(
      WebFeature::
          kNonNoneTouchActionWouldLoseEditableHandwritingRestoredByScroller));
}

// Tests that the use counter that follows the panning rules wouldn't be fired
// when it is re-stablished by a scrollable div, but that the use counter that
// follows the pinch-zoom rules would.
TEST_F(StyleAdjusterTest, TouchActionHandwriting_NotInhibitedByParent) {
  SetBodyInnerHTML(R"HTML(
    <style>#ancestor { margin: 0; touch-action: pinch-zoom; }
    #scroller { overflow: scroll; width: 100px; height: 100px; }
    #target { width: 200px; height: 200px; } </style>
    <div id='ancestor'><div id='scroller'><div contenteditable="true" id='target'>
    </div></div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  Element* text = GetDocument().getElementById(AtomicString("target"));
  EXPECT_NE(TouchAction::kNone,
            TouchAction::kInternalHandwritingPanningRules &
                text->GetComputedStyle()->EffectiveTouchAction());
  EXPECT_TRUE(GetDocument().IsUseCounted(
      WebFeature::kNonNoneTouchActionWouldLoseEditableHandwriting));
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::
          kNonNoneTouchActionWouldLoseEditableHandwritingRestoredByScroller));
}

// Similar to above, but with nested scrollers.
TEST_F(StyleAdjusterTest, TouchActionHandwriting_NestedScrollers) {
  SetBodyInnerHTML(R"HTML(
    <style>#ancestor { margin: 0; touch-action: pinch-zoom; }
    #scroller { overflow: scroll; width: 200px; height: 200px; touch-action: none; }
    #scroller_2 { overflow: scroll; width: 100px; height: 100px; }
    #target { width: 200px; height: 200px; } </style>
    <div id='ancestor'><div id='scroller'><div id='scroller_2'><div contenteditable="true" id='target'>
    </div></div></div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  Element* text = GetDocument().getElementById(AtomicString("target"));
  EXPECT_NE(TouchAction::kNone,
            TouchAction::kInternalHandwritingPanningRules &
                text->GetComputedStyle()->EffectiveTouchAction());
  EXPECT_TRUE(GetDocument().IsUseCounted(
      WebFeature::kNonNoneTouchActionWouldLoseEditableHandwriting));
  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::
          kNonNoneTouchActionWouldLoseEditableHandwritingRestoredByScroller));
}

TEST_F(StyleAdjusterTest, TouchActionHandwriting_ScrollerHasItsOwnStyle) {
  SetBodyInnerHTML(R"HTML(
    <style>#ancestor { margin: 0; touch-action: pinch-zoom; }
    #scroller { overflow: scroll; width: 100px; height: 100px; touch-action: pinch-zoom; }
    #target { width: 200px; height: 200px; } </style>
    <div id='ancestor'><div id='scroller'><div contenteditable="true" id='target'>
    </div></div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  Element* text = GetDocument().getElementById(AtomicString("target"));
  EXPECT_EQ(TouchAction::kNone,
            TouchAction::kInternalHandwritingPanningRules &
                text->GetComputedStyle()->EffectiveTouchAction());
  EXPECT_TRUE(GetDocument().IsUseCounted(
      WebFeature::kNonNoneTouchActionWouldLoseEditableHandwriting));
  EXPECT_TRUE(GetDocument().IsUseCounted(
      WebFeature::
          kNonNoneTouchActionWouldLoseEditableHandwritingRestoredByScroller));
}

TEST_F(StyleAdjusterTest, OverflowClipUseCount) {
  GetDocument().SetBaseURLOverride(KURL("http://test.com"));
  SetBodyInnerHTML(R"HTML(
    <div></div>
    <div style='overflow: hidden'></div>
    <div style='overflow: scroll'></div>
    <div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(
      GetDocument().IsUseCounted(WebFeature::kOverflowClipAlongEitherAxis));

  SetBodyInnerHTML(R"HTML(
    <div style='overflow: clip'></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(
      GetDocument().IsUseCounted(WebFeature::kOverflowClipAlongEitherAxis));
}

// crbug.com/392643253
TEST_F(StyleAdjusterTest, AdjustForDisplayInlinify) {
  SetBodyInnerHTML(R"HTML(<ruby><video></video><audio></audio></ruby>)HTML");
  UpdateAllLifecyclePhasesForTest();
  // Pass if no crashes.
}

// crbug.com/1216721
TEST_F(StyleAdjusterTest, AdjustForSVGCrash) {
  SetBodyInnerHTML(R"HTML(
<style>
.class1 { dominant-baseline: hanging; }
</style>
<svg>
<tref>
<text id="text5" style="dominant-baseline: no-change;"/>
</svg>
<svg>
<use id="use1" xlink:href="#text5" class="class1" />
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  Element* text = GetDocument()
                      .getElementById(AtomicString("use1"))
                      ->GetShadowRoot()
                      ->getElementById(AtomicString("text5"));
  EXPECT_EQ(EDominantBaseline::kHanging,
            text->GetComputedStyle()->CssDominantBaseline());
}

}  // namespace blink
