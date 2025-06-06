// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview 'localized-link' takes a localized string that
 * contains up to one anchor tag, and labels the string contained within the
 * anchor tag with the entire localized string. The string should not be bound
 * by element tags. The string should not contain any elements other than the
 * single anchor tagged element that will be aria-labelledby the entire string.
 *
 * Example: "lorem ipsum <a href="example.com">Learn More</a> dolor sit"
 *
 * The "Learn More" will be aria-labelledby like so: "lorem ipsum Learn More
 * dolor sit". Meanwhile, "Lorem ipsum" and "dolor sit" will be aria-hidden.
 *
 * This element also supports strings that do not contain anchor tags; in this
 * case, the element gracefully falls back to normal text. This can be useful
 * when the property is data-bound to a function which sometimes returns a
 * string with a link and sometimes returns a normal string.
 */

import {assert, assertNotReached} from '//resources/js/assert.js';
import {sanitizeInnerHtml} from '//resources/js/parse_html_subset.js';
import type {PropertyValues} from '//resources/lit/v3_0/lit.rollup.js';
import {CrLitElement} from '//resources/lit/v3_0/lit.rollup.js';

import {getCss} from './localized_link.css.js';
import {getHtml} from './localized_link.html.js';

export interface LocalizedLinkElement {
  $: {
    container: HTMLElement,
  };
}

export class LocalizedLinkElement extends CrLitElement {
  static get is() {
    return 'localized-link';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      /**
       * The localized string that contains up to one anchor tag, the text
       * within which will be aria-labelledby the entire localizedString.
       */
      localizedString: {type: String},

      /**
       * If provided, the URL that the anchor tag will point to. There is no
       * need to provide a linkUrl if the URL is embedded in the
       * localizedString.
       */
      linkUrl: {type: String},

      /**
       * If true, localized link will be disabled.
       */
      linkDisabled: {
        type: Boolean,
        reflect: true,
      },

      /**
       * localizedString, with aria attributes and the optionally provided
       * link.
       */
      containerInnerHTML_: {type: String},
    };
  }

  accessor localizedString: string = '';
  accessor linkUrl: string = '';
  accessor linkDisabled: boolean = false;
  private accessor containerInnerHTML_: string = '';

  override willUpdate(changedProperties: PropertyValues<this>) {
    super.willUpdate(changedProperties);
    if (changedProperties.has('localizedString') ||
        changedProperties.has('linkUrl')) {
      this.containerInnerHTML_ =
          this.getAriaLabelledContent_(this.localizedString, this.linkUrl);
    }
  }

  override updated(changedProperties: PropertyValues<this>) {
    super.updated(changedProperties);

    const changedPrivateProperties =
        changedProperties as Map<PropertyKey, unknown>;

    if (changedPrivateProperties.has('containerInnerHTML_')) {
      this.setContainerInnerHtml_();
    }

    if (changedProperties.has('linkDisabled')) {
      this.updateAnchorTagTabIndex_();
    }
  }

  /**
   * Attaches aria attributes and optionally provided link to the provided
   * localizedString.
   * @return localizedString formatted with additional ids, spans, and an
   *     aria-labelledby tag
   */
  private getAriaLabelledContent_(localizedString: string, linkUrl: string):
      string {
    const tempEl = document.createElement('div');
    tempEl.innerHTML = sanitizeInnerHtml(localizedString, {attrs: ['id']});

    const ariaLabelledByIds: string[] = [];
    tempEl.childNodes.forEach((node, index) => {
      // Text nodes should be aria-hidden and associated with an element id
      // that the anchor element can be aria-labelledby.
      if (node.nodeType === Node.TEXT_NODE) {
        const spanNode = document.createElement('span');
        spanNode.textContent = node.textContent;
        spanNode.id = `id${index}`;
        ariaLabelledByIds.push(spanNode.id);
        spanNode.setAttribute('aria-hidden', 'true');
        node.replaceWith(spanNode);
        return;
      }
      // The single element node with anchor tags should also be aria-labelledby
      // itself in-order with respect to the entire string.
      if (node.nodeType === Node.ELEMENT_NODE && node.nodeName === 'A') {
        const element = node as HTMLAnchorElement;
        element.id = `id${index}`;
        ariaLabelledByIds.push(element.id);
        return;
      }

      // Only text and <a> nodes are allowed.
      assertNotReached('localized-link has invalid node types');
    });

    const anchorTags = tempEl.querySelectorAll('a');
    // In the event the provided localizedString contains only text nodes,
    // populate the contents with the provided localizedString.
    if (anchorTags.length === 0) {
      return localizedString;
    }

    assert(
        anchorTags.length === 1,
        'localized-link should contain exactly one anchor tag');
    const anchorTag = anchorTags[0]!;
    anchorTag.setAttribute('aria-labelledby', ariaLabelledByIds.join(' '));
    anchorTag.tabIndex = this.linkDisabled ? -1 : 0;

    if (linkUrl !== '') {
      anchorTag.href = linkUrl;
      anchorTag.target = '_blank';
    }

    return tempEl.innerHTML;
  }

  private setContainerInnerHtml_() {
    this.$.container.innerHTML = sanitizeInnerHtml(this.containerInnerHTML_, {
      attrs: [
        'aria-hidden',
        'aria-labelledby',
        'id',
        'tabindex',
      ],
    });
    const anchorTag = this.shadowRoot.querySelector('a');
    if (anchorTag) {
      anchorTag.addEventListener(
          'click', (event) => this.onAnchorTagClick_(event));
      anchorTag.addEventListener('auxclick', (event) => {
        // trigger the click handler on middle-button clicks
        if (event.button === 1) {
          this.onAnchorTagClick_(event);
        }
      });
    }
  }

  private onAnchorTagClick_(event: Event) {
    if (this.linkDisabled) {
      event.preventDefault();
      return;
    }
    this.fire('link-clicked', {event});
    // Stop propagation of the event, since it has already been handled by
    // opening the link.
    event.stopPropagation();
  }

  /**
   *  Removes anchor tag from being targeted by chromeVox when link is
   *  disabled.
   */
  private updateAnchorTagTabIndex_() {
    const anchorTag = this.shadowRoot.querySelector('a');
    if (!anchorTag) {
      return;
    }
    anchorTag.tabIndex = this.linkDisabled ? -1 : 0;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'localized-link': LocalizedLinkElement;
  }
}

customElements.define(LocalizedLinkElement.is, LocalizedLinkElement);
