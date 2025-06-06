// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CONTENT_RENDERER_FORM_AUTOFILL_UTIL_H_
#define COMPONENTS_AUTOFILL_CONTENT_RENDERER_FORM_AUTOFILL_UTIL_H_

#include <stddef.h>

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/i18n/rtl.h"
#include "components/autofill/content/renderer/timing.h"
#include "components/autofill/core/common/autofill_constants.h"
#include "components/autofill/core/common/dense_set.h"
#include "components/autofill/core/common/form_data.h"
#include "components/autofill/core/common/form_field_data.h"
#include "components/autofill/core/common/mojom/autofill_types.mojom-shared.h"
#include "components/autofill/core/common/unique_ids.h"
#include "third_party/blink/public/web/web_autofill_state.h"
#include "third_party/blink/public/web/web_element_collection.h"
#include "third_party/blink/public/web/web_form_control_element.h"

class GURL;

namespace blink {
enum class WebAutofillState;

class WebDocument;
class WebElement;
class WebFormControlElement;
class WebFormElement;
class WebInputElement;
class WebLocalFrame;
class WebNode;
}  // namespace blink

namespace content {
class RenderFrame;
}  // namespace content

namespace autofill {

class FieldDataManager;
class FormData;
class FormFieldData;
class SynchronousFormCache;

namespace form_util {

// This file contains utility function related to form and form field
// extraction, label inference, DOM traversal, and form field preview and
// autofilling.
//
// See README.md for the terminology used in this file.

// Mapping from a form element's render id to results of button titles
// heuristics for a given form element.
using ButtonTitlesCache = base::flat_map<FormRendererId, ButtonTitleList>;

// A bit field mask to extract data from WebFormControlElement.
// Copied to components/autofill/ios/browser/resources/autofill_controller.js.
enum class ExtractOption {
  kBounds,    // Extract bounds from WebFormControlElement, could
              // trigger layout if needed.
  kDatalist,  // Extract datalist from WebFormControlElement, the total
              // number of options is up to kMaxListSize and each option
              // has as far as kMaxDataLength.
  kMinValue = kBounds,
  kMaxValue = kDatalist,
};

// Extract FormData from `form_element` or the unowned form if
// `form_element.IsNull()`.
//
// The document must be the one we want to extract fields from. In other words:
// Do not blindly pass "some" WebDocument for `document`! If `form_element` is
// non-null, `document` must obviously be `form_element`'s document. As a rule
// of thumb, avoid passing "the current frame's document" but instead, whenever
// possible, pass WebForm[Control]Element::GetDocument() of the form or of the
// field whose form that we want to extract.
std::optional<FormData> ExtractFormData(
    const blink::WebDocument& document,
    const blink::WebFormElement& form_element,
    const FieldDataManager& field_data_manager,
    const CallTimerState& timer_state,
    ButtonTitlesCache* button_titles_cache,
    DenseSet<ExtractOption> extract_options = {});

// Helper function to assist in getting the canonical form of the action and
// origin. The action will properly take into account <BASE>, and both will
// strip unnecessary data (e.g. query params and HTTP credentials).
GURL GetCanonicalActionForForm(const blink::WebFormElement& form);

// Returns true if `element` is a textarea element.
bool IsTextAreaElement(const blink::WebFormControlElement& element);

// Returns true if `element` is a textarea element or a text input element.
bool IsTextAreaElementOrTextInput(const blink::WebFormControlElement& element);

// Returns true if `element` is connected and not in a user-agent tree.
bool IsAccessible(const blink::WebNode& node);

// Returns true if `element` is one of the element types that can be autofilled.
// {Text, Radiobutton, Checkbox, Select, TextArea}.
// TODO(crbug.com/40100455): IsAutofillableElement() are currently used
// inconsistently. Investigate where these checks are necessary.
bool IsAutofillableElement(const blink::WebFormControlElement& element);

// Returns the current FormControlType of `element` or kInputPassword if
// `element` ever was an <input type=password>.
std::optional<FormControlType> GetAutofillFormControlType(
    const blink::WebFormControlElement& element);

// Returns true iff `element` has a "webauthn" autocomplete attribute.
bool IsWebauthnTaggedElement(const blink::WebFormControlElement& element);

// Returns true if |element| can be edited (enabled and not read only).
bool IsElementEditable(const blink::WebInputElement& element);

// Returns the FormRendererId of a given WebFormElement or contenteditable. If
// WebFormElement::IsNull(), returns a null form renderer id, which is the
// renderer id of the unowned form.
FormRendererId GetFormRendererId(const blink::WebElement& e);

// Returns the FieldRendererId of a given WebFormControlElement or
// contenteditable.
FieldRendererId GetFieldRendererId(const blink::WebElement& e);

// Returns text alignment for |element|.
base::i18n::TextDirection GetTextDirectionForElement(
    const blink::WebFormControlElement& element);

// Returns all connected, autofillable form control elements
// - owned by `form_element` if `!form_element.IsNull()`;
// - owned by no form otherwise.
std::vector<blink::WebFormControlElement> GetOwnedAutofillableFormControls(
    const blink::WebDocument& document,
    const blink::WebFormElement& form_element);

// Extracts the FormData that represents the form of `element`. If that form
// cannot be extracted (e.g., because it is too large), falls back to a
// single-field form that contains `element`. If however `element` is not
// autofillable, returns nullopt. `form_cache` can be used to optimize form
// extractions occurring synchronously after this function call.
std::optional<std::pair<FormData, raw_ref<const FormFieldData>>>
FindFormAndFieldForFormControlElement(
    const blink::WebFormControlElement& element,
    const FieldDataManager& field_data_manager,
    const CallTimerState& timer_state,
    form_util::ButtonTitlesCache* button_titles_cache,
    DenseSet<ExtractOption> extract_options,
    const SynchronousFormCache& form_cache);

// Creates a FormData containing a single field out of a contenteditable
// non-form element. The FormData is synthetic in the sense that it does not
// correspond to any other DOM element. It is also conceptually distinct from
// the unowned form (i.e., the collection of form control elements that aren't
// owned by any form).
//
// Returns `std::nullopt` if `contenteditable`:
// - is a WebFormElement; otherwise, there could be two FormData objects with
//   identical renderer ID referring to different conceptual forms: the one for
//   the contenteditable and an actual <form>.
// - is a WebFormControlElement; otherwise, a <textarea contenteditable> might
//   be a member of two FormData objects: the one for the contenteditable and
//   the <textarea>'s associated <form>'s FormData.
// - has a contenteditable parent; this is to disambiguate focus elements on
//   nested contenteditables because the focus event propagates up.
//
// The FormData's renderer ID has the same value as its (single) FormFieldData's
// renderer ID. This is collision-free with the renderer IDs of any other form
// in the document because DomNodeIds are unique among all DOM elements.
std::optional<FormData> FindFormForContentEditable(
    const blink::WebElement& content_editable);

// Fills or previews the fields represented by `fields`.
// `initiating_element` is the element that initiated the autofill process.
// Returns a list of pairs of the filled elements and their autofill state
// prior to the filling.
std::vector<std::pair<FieldRendererId, blink::WebAutofillState>>
ApplyFieldsAction(const blink::WebDocument& document,
                  base::span<const FormFieldData::FillData> fields,
                  mojom::FormActionType action_type,
                  mojom::ActionPersistence action_persistence,
                  FieldDataManager& field_data_manager);

// Clears the suggested values in `previewed_elements`.
// `initiating_element` is the element that initiated the preview operation.
// `old_autofill_state` is the previous state of the field that initiated the
// preview.
void ClearPreviewedElements(
    base::span<std::pair<blink::WebFormControlElement, blink::WebAutofillState>>
        previewed_elements);

// Indicates if |node| is owned by |frame| in the sense of
// https://dom.spec.whatwg.org/#concept-node-document. Note that being owned by
// a frame does not require being attached to its DOM.
bool IsOwnedByFrame(const blink::WebNode& node, content::RenderFrame* frame);

// Returns true if `node` is currently owned by `frame` or its frame is nullptr,
// in which case the frame is not known anymore. It is a weaker condition than
// `IsOwnedByFrame(node, frame)`.
bool MaybeWasOwnedByFrame(const blink::WebNode& node,
                          content::RenderFrame* frame);

// Checks if the webpage is empty.
// This kind of webpage is considered as empty:
// <html>
//    <head>
//    </head>
//    <body>
//    </body>
// </html>
// Meta, script and title tags don't influence the emptiness of a webpage.
bool IsWebpageEmpty(const blink::WebLocalFrame* frame);

// Returns the aggregated values of the descendants of |element| that are
// non-empty text nodes.  This is a faster alternative to |innerText()| for
// performance critical operations.  It does a full depth-first search so can be
// used when the structure is not directly known.  However, unlike with
// |innerText()|, the search depth and breadth are limited to a fixed threshold.
// Whitespace is trimmed from text accumulated at descendant nodes.
std::u16string FindChildText(const blink::WebNode& node);

// Returns the button titles for |web_form|. |button_titles_cache| can be used
// to spare recomputation if called multiple times for the same form.
ButtonTitleList GetButtonTitles(const blink::WebFormElement& web_form,
                                ButtonTitlesCache* button_titles_cache);

// Returns the form element by unique renderer id. Returns the null element if
// there is no form with the |form_renderer_id|.
blink::WebFormElement GetFormByRendererId(FormRendererId form_renderer_id);

// Returns the form control element by unique renderer id.
// |form_to_be_searched| could be used as an optimization to only search for
// elements in it, but doesn't guarantee that the returned element will belong
// to it. Returns the null element if there is no element with the
// |queried_form_control| renderer id.
blink::WebFormControlElement GetFormControlByRendererId(
    FieldRendererId queried_form_control);

blink::WebElement GetContentEditableByRendererId(
    FieldRendererId field_renderer_id);

std::string GetAutocompleteAttribute(const blink::WebElement& element);

// Iterates through the node neighbors of form and form control elements in
// `document` in search of four digit combinations.
void TraverseDomForFourDigitCombinations(
    const blink::WebDocument& document,
    base::OnceCallback<void(const std::vector<std::string>&)>
        potential_matches);

// This algorithm attempts to extract the final-checkout-amount using regex
// matching. Since the document may list the prices of individual items, a
// second regex is used to identify order-total labels. It is assumed that the
// true final-checkout-amount node is the price node that is the shortest
// distance to a label node. The distance of a price node to a label node is
// measured by the length of the path from the price node to their lowest common
// ancestor. If there are multiple such nodes, returning the first match is
// fine. The returned string is empty if a final-checkout-amount is not found,
// and a string (including dollar signs, periods, and commas) of the
// final-checkout-amount text node value if one is found. The
// final-checkout-amount-text-node is the text node that is deemed to contain
// the final-checkout-amount of the checkout page (ex: a text node containing
// the text "$100.00").
//
// `price_regex` is a regex that is used to check if a text node is a price node
// (and all price nodes are potential final-checkout-amount-nodes).
// `label_regex` is a regex that is used to check if a text node is a label
// node. Label nodes are nodes that, if found near price nodes, are deemed to
// label that price node as a final-checkout-amount. They contain text that
// implies a final-checkout-amount, such as "Order total" or "Total amount".
// `number_of_ancestor_levels_to_search` denotes how many levels of ancestors of
// price nodes should be searched to look for a label node.
//
// Some features in Payments Autofill need to know the final-checkout-amount of
// a page to work properly, for example BNPL. This algorithm attempts to extract
// the final-checkout-amount from the page. It will not always be reliable, but
// from manual testing it works 90%+ of the time.
//
// Example:
// <div>
//   <div>
//     <span>
//       <span>$56.70</span>
//     </span>
//     <span>
//       <span>Total amount:</span>
//     </span>
//   </div>
//   <div>
//     <div>
//       <div>
//         <span>
//           <span>$100.00</span>
//         </span>
//       </div>
//     </div>
//   </div>
// </div>
//
// In the example above, the search will start at the price nodes "$56.70" and
// "$100.00", then go up and search the subtrees of their ancestors. 2 ancestor
// levels up from the "$56.70" price node, it will reach the 2nd <div> block,
// and find the label node "Total amount:" in its subtree, thus returning the
// final-checkout-amount-node's value as "$56.70". Since the $100.00 price node
// is further away, it will not be considered as the final-checkout-amount.
std::string ExtractFinalCheckoutAmountFromDom(
    const blink::WebDocument& document,
    std::string_view price_regex,
    std::string_view label_regex,
    size_t number_of_ancestor_levels_to_search);

// Attempts to update `FormFieldData::user_input_` of `field`, whose DOM element
// is identified by `element_id`, using `field_data_manager`.
void MaybeUpdateUserInput(FormFieldData& field,
                          FieldRendererId element_id,
                          const FieldDataManager& field_data_manager);

// The following functions exist in as internal helper functions in
// form_autofill_util.cc and are exposed here just for testing purposes. Check
// the wrapped functions in the .cc file for documentation.
std::vector<blink::WebFormControlElement> GetOwnedFormControlsForTesting(
    const blink::WebDocument& document,
    const blink::WebFormElement& form_element);
blink::WebNode NextWebNodeForTesting(const blink::WebNode& current_node,
                                     bool forward);
std::u16string GetAriaLabelForTesting(const blink::WebDocument& document,
                                      const blink::WebElement& element);
std::u16string GetAriaDescriptionForTesting(const blink::WebDocument& document,
                                            const blink::WebElement& element);
void InferLabelForElementsForTesting(
    base::span<const blink::WebFormControlElement> control_elements,
    std::vector<FormFieldData>& fields);
std::u16string FindChildTextWithIgnoreListForTesting(
    const blink::WebNode& node,
    const std::set<blink::WebNode>& divs_to_skip);
std::vector<SelectOption> GetDataListOptionsForTesting(
    const blink::WebInputElement& element);
blink::WebFormElement GetClosestAncestorFormElementForTesting(blink::WebNode n);
bool IsDOMPredecessorForTesting(const blink::WebNode& x,
                                const blink::WebNode& y,
                                const blink::WebNode& ancestor_hint);
bool IsWebElementVisibleForTesting(const blink::WebElement& element);
bool IsVisibleIframeForTesting(const blink::WebElement& iframe_element);
uint64_t GetMaxLengthForTesting(const blink::WebFormControlElement& element);
void WebFormControlElementToFormFieldForTesting(
    const blink::WebFormElement& form_element,
    const blink::WebFormControlElement& element,
    const FieldDataManager* field_data_manager,
    DenseSet<ExtractOption> extract_options,
    FormFieldData* field);

}  // namespace form_util
}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CONTENT_RENDERER_FORM_AUTOFILL_UTIL_H_
