// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Handle styling a 'CHROME_ANNOTATION' element.
 */

const DECORATION_STYLES = 'border-bottom-width: 1px; ' +
    'border-bottom-style: dotted; ' +
    'background-color: transparent; ' +
    'pointer-events: none;';
const DECORATION_STYLES_FOR_PHONE_AND_EMAIL = 'border-bottom-width: 1px; ' +
    'border-bottom-style: solid; ' +
    'background-color: transparent; ' +
    'pointer-events: none;';
const DECORATION_DEFAULT_COLOR = 'blue';
const DECORATION_STYLES_FOR_SPACE = 'white-space: pre';

// Class to add style to annotations `Element`.
export class TextStyler {
  // Adds style on given `element`.
  style(parentNode: Node, element: HTMLElement, type: string): void {
    let textColor: string = DECORATION_DEFAULT_COLOR;
    if (parentNode instanceof Element) {
      textColor = window.getComputedStyle(parentNode).color || textColor;
    }

    if (type === 'PHONE_NUMBER' || type === 'EMAIL') {
      element.style.cssText = DECORATION_STYLES_FOR_PHONE_AND_EMAIL;
    } else {
      element.style.cssText = DECORATION_STYLES;
    }

    element.style.borderBottomColor = textColor;
    element.setAttribute('role', 'link');
  }

  // Styles a whitespace that doesn't collapse with its neighbors.
  space(element: HTMLElement): void {
    element.style.cssText = DECORATION_STYLES_FOR_SPACE;
  }
}
