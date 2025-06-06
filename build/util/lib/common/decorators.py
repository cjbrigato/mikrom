# Copyright 2017 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import functools
import logging


def Memoize(f):
  """Decorator to cache return values of function."""
  memoize_dict = {}

  @functools.wraps(f)
  def wrapper(*args, **kwargs):
    key = repr((args, kwargs))
    if key not in memoize_dict:
      memoize_dict[key] = f(*args, **kwargs)
    return memoize_dict[key]

  return wrapper


def NoRaiseException(default_return_value=None,
                     exception_type=None,
                     exception_message=''):
  """Returns decorator that catches and logs uncaught Exceptions.

  Args:
    default_return_value: Value to return in the case of uncaught Exception.
    exception_type: Type of exception to catch. If None, all exceptions are
      caught.
    exception_message: Message for uncaught exceptions.
  """

  def decorator(f):

    @functools.wraps(f)
    def wrapper(*args, **kwargs):
      _exception_type = exception_type or Exception
      try:
        return f(*args, **kwargs)
      except _exception_type as e:  # pylint: disable=broad-except
        logging.exception(exception_message, e)
        return default_return_value

    return wrapper

  return decorator
