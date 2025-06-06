#!/usr/bin/env python3
# Copyright 2012 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import itertools
import json
import os.path
import pprint
import re
import sys

from json_parse import OrderedDict

# This file is a peer to json_schema.py. Each of these files understands a
# certain format describing APIs (either JSON or IDL), reads files written
# in that format into memory, and emits them as a Python array of objects
# corresponding to those APIs, where the objects are formatted in a way that
# the JSON schema compiler understands. compiler.py drives both idl_schema.py
# and json_schema.py.

# idl_parser expects to be able to import certain files in its directory,
# so let's set things up the way it wants.
_idl_generators_path = os.path.join(os.path.dirname(os.path.realpath(__file__)),
                                    os.pardir, os.pardir, 'ppapi', 'generators')
if _idl_generators_path in sys.path:
  import idl_parser
else:
  sys.path.insert(0, _idl_generators_path)
  try:
    import idl_parser
  finally:
    sys.path.pop(0)


def ProcessComment(comment):
  '''
  Convert a comment into a parent comment and a list of parameter comments.

  Function comments are of the form:
    Function documentation. May contain HTML and multiple lines.

    |arg1_name|: Description of arg1. Use <var>argument</var> to refer
    to other arguments.
    |arg2_name|: Description of arg2...

  Newlines are removed, and leading and trailing whitespace is stripped.

  Args:
    comment: The string from a Comment node.

  Returns: A tuple that looks like:
    (
      "The processed comment, minus all |parameter| mentions.",
      {
        'parameter_name_1': "The comment that followed |parameter_name_1|:",
        ...
      }
    )
  '''

  def add_paragraphs(content):
    paragraphs = content.split('\n\n')
    if len(paragraphs) < 2:
      return content
    return '<p>' + '</p><p>'.join(p.strip() for p in paragraphs) + '</p>'

  # Find all the parameter comments of the form '|name|: comment'.
  parameter_starts = list(re.finditer(r' *\|([^|]*)\| *: *', comment))

  # Get the parent comment (everything before the first parameter comment.
  first_parameter_location = (parameter_starts[0].start()
                              if parameter_starts else len(comment))
  parent_comment = (add_paragraphs(
      comment[:first_parameter_location].strip()).replace('\n', ''))

  params = OrderedDict()
  for (cur_param, next_param) in itertools.zip_longest(parameter_starts,
                                                       parameter_starts[1:]):
    param_name = cur_param.group(1)

    # A parameter's comment goes from the end of its introduction to the
    # beginning of the next parameter's introduction.
    param_comment_start = cur_param.end()
    param_comment_end = next_param.start() if next_param else len(comment)
    params[param_name] = (add_paragraphs(
        comment[param_comment_start:param_comment_end].strip()).replace(
            '\n', ''))

  return (parent_comment, params)


class Callspec(object):
  '''
  Given a Callspec node representing an IDL function declaration, converts into
  a tuple:
      (name, list of function parameters, return type, async return)
  '''

  def __init__(self, callspec_node, comment):
    self.node = callspec_node
    self.comment = comment

  def process(self, use_returns_async, callbacks):
    parameters = []
    return_type = None
    returns_async = None
    if self.node.GetProperty('TYPEREF') not in ('void', None):
      return_type = Typeref(self.node.GetProperty('TYPEREF'), self.node.parent,
                            {
                                'name': self.node.GetName()
                            }).process(callbacks)
      # The IDL parser doesn't allow specifying return types as optional.
      # Instead we infer any object return values to be optional.
      # TODO(asargent): fix the IDL parser to support optional return types.
      if return_type.get('type') == 'object' or '$ref' in return_type:
        return_type['optional'] = True
    for node in self.node.GetChildren():
      parameter = Param(node).process(callbacks)
      if parameter['name'] in self.comment:
        parameter['description'] = self.comment[parameter['name']]
      parameters.append(parameter)
    # At the moment all functions in IDL with an asynchronous return are defined
    # with a trailing callback in their parameters, but in our schema model we
    # represent this with a separate returns 'async_field'. If there is a
    # trailing callback, pop it off into the returns asyc property.
    # Note: We only do this for interface types of 'Functions' and 'Properties',
    # not for 'Events' and IDL callback definitions (specified by the
    # |use_returns_async parameter|) or for Function definitions with trailing
    # callbacks which are not asynchronous returns (specified by the
    # trailingCallbackIsFunctionParameter extended attribute).
    # TODO(tjudkins): Once IDL definitions are changed to describe returning
    # promises, we can condition on that instead.
    if (use_returns_async
        and not self.node.GetProperty('trailingCallbackIsFunctionParameter')
        and len(parameters) > 0 and parameters[-1].get('type') == 'function'):
      returns_async = parameters.pop()
      # The returns_async field is inherently a function, so doesn't need type
      # specified on it.
      returns_async.pop('type')
      does_not_support_promises = self.node.GetProperty(
          'doesNotSupportPromises')
      if does_not_support_promises is not None:
        returns_async['does_not_support_promises'] = does_not_support_promises
      else:
        assert return_type is None, (
            'Function "%s" cannot support promises and also have a '
            'return value.' % self.node.GetName())
    else:
      assert not self.node.GetProperty('doesNotSupportPromises'), (
          'Callspec "%s" does not need to specify [doesNotSupportPromises] if '
          'it does not have a trailing callback' % self.node.GetName())

    return (self.node.GetName(), parameters, return_type, returns_async)


class Param(object):
  '''
  Given a Param node representing a function parameter, converts into a Python
  dictionary that the JSON schema compiler expects to see.
  '''

  def __init__(self, param_node):
    self.node = param_node

  def process(self, callbacks):
    return Typeref(self.node.GetProperty('TYPEREF'), self.node, {
        'name': self.node.GetName()
    }).process(callbacks)


class Dictionary(object):
  '''
  Given an IDL Dictionary node, converts into a Python dictionary that the JSON
  schema compiler expects to see.
  '''

  def __init__(self, dictionary_node):
    self.node = dictionary_node

  def process(self, callbacks):
    properties = OrderedDict()
    for node in self.node.GetChildren():
      if node.cls == 'Member':
        k, v = Member(node).process(callbacks)
        properties[k] = v
    result = {
        'id': self.node.GetName(),
        'properties': properties,
        'type': 'object'
    }
    # If this has the `ignoreAdditionalProperties` extended attribute, copy it
    # into the resulting object with a value of True.
    if self.node.GetProperty('ignoreAdditionalProperties'):
      result['ignoreAdditionalProperties'] = True

    if self.node.GetProperty('nodoc'):
      result['nodoc'] = True
    return result


class Member(object):
  '''
  Given an IDL dictionary or interface member, converts into a name/value pair
  where the value is a Python dictionary that the JSON schema compiler expects
  to see.
  '''

  def __init__(self, member_node):
    self.node = member_node

  def process(self,
              callbacks,
              functions_are_properties=False,
              use_returns_async=False):
    properties = OrderedDict()
    name = self.node.GetName()
    if self.node.GetProperty('deprecated'):
      properties['deprecated'] = self.node.GetProperty('deprecated')

    for property_name in [
        'nodoc', 'nocompile', 'nodart', 'serializableFunction'
    ]:
      if self.node.GetProperty(property_name):
        properties[property_name] = True

    if self.node.GetProperty('OPTIONAL'):
      properties['optional'] = True

    if self.node.GetProperty('platforms'):
      properties['platforms'] = list(self.node.GetProperty('platforms'))

    for option_name, sanitizer in [('maxListeners', int),
                                   ('supportsFilters', lambda s: s == 'true'),
                                   ('supportsListeners', lambda s: s == 'true'),
                                   ('supportsRules', lambda s: s == 'true')]:
      if self.node.GetProperty(option_name):
        if 'options' not in properties:
          properties['options'] = {}
        properties['options'][option_name] = sanitizer(
            self.node.GetProperty(option_name))
    type_override = None
    parameter_comments = OrderedDict()
    for node in self.node.GetChildren():
      if node.cls == 'Comment':
        (parent_comment, parameter_comments) = ProcessComment(node.GetName())
        properties['description'] = parent_comment
      elif node.cls == 'Callspec':
        name, parameters, return_type, returns_async = Callspec(
            node, parameter_comments).process(use_returns_async, callbacks)
        if functions_are_properties:
          # If functions are treated as properties (which will happen if the
          # interface is named Properties) then this isn't a function, it's a
          # property which is encoded as a function with no arguments. The
          # property type is the return type. This is an egregious hack in lieu
          # of the IDL parser supporting 'const'.
          assert parameters == [], (
              'Property "%s" must be no-argument functions '
              'with a non-void return type' % name)
          assert return_type is not None, (
              'Property "%s" must be no-argument functions '
              'with a non-void return type' % name)
          assert 'type' in return_type, (
              'Property return type "%s" from "%s" must specify a '
              'fundamental IDL type.' % (pprint.pformat(return_type), name))
          type_override = return_type['type']
        else:
          type_override = 'function'
          properties['parameters'] = parameters
          if return_type is not None:
            properties['returns'] = return_type
          if returns_async is not None:
            properties['returns_async'] = returns_async

    properties['name'] = name
    if type_override is not None:
      properties['type'] = type_override
    else:
      properties = Typeref(self.node.GetProperty('TYPEREF'), self.node,
                           properties).process(callbacks)
    value = self.node.GetProperty('value')
    if value is not None:
      # IDL always returns values as strings, so cast to their real type.
      properties['value'] = self.cast_from_json_type(properties['type'], value)
    return name, properties

  def cast_from_json_type(self, json_type, string_value):
    '''Casts from string |string_value| to a real Python type based on a JSON
    Schema type |json_type|. For example, a string value of '42' and a JSON
    Schema type 'integer' will cast to int('42') ==> 42.
    '''
    if json_type == 'integer':
      return int(string_value)
    if json_type == 'number':
      return float(string_value)
    # Add more as necessary.
    assert json_type == 'string', (
        'No rule exists to cast JSON Schema type "%s" to its equivalent '
        'Python type for value "%s". You must add a new rule here.' %
        (json_type, string_value))
    return string_value


class Typeref(object):
  '''
  Given a TYPEREF property representing the type of dictionary member or
  function parameter, converts into a Python dictionary that the JSON schema
  compiler expects to see.
  '''

  def __init__(self, typeref, parent, additional_properties):
    self.typeref = typeref
    self.parent = parent
    self.additional_properties = additional_properties

  def process(self, callbacks):
    properties = self.additional_properties
    result = properties

    if self.parent.GetPropertyLocal('OPTIONAL'):
      properties['optional'] = True

    # The IDL parser denotes array types by adding a child 'Array' node onto
    # the Param node in the Callspec.
    for sibling in self.parent.GetChildren():
      if sibling.cls == 'Array' and sibling.GetName() == self.parent.GetName():
        properties['type'] = 'array'
        properties['items'] = OrderedDict()
        properties = properties['items']
        break

    if self.typeref == 'DOMString':
      properties['type'] = 'string'
    elif self.typeref == 'boolean':
      properties['type'] = 'boolean'
    elif self.typeref == 'double':
      properties['type'] = 'number'
    elif self.typeref == 'long':
      properties['type'] = 'integer'
    elif self.typeref == 'any':
      properties['type'] = 'any'
    elif self.typeref == 'object':
      properties['type'] = 'object'
      if 'additionalProperties' not in properties:
        properties['additionalProperties'] = OrderedDict()
      properties['additionalProperties']['type'] = 'any'
      instance_of = self.parent.GetProperty('instanceOf')
      if instance_of:
        properties['isInstanceOf'] = instance_of
    elif self.typeref == 'ArrayBuffer':
      properties['type'] = 'binary'
      properties['isInstanceOf'] = 'ArrayBuffer'
    elif self.typeref == 'ArrayBufferView':
      properties['type'] = 'binary'
      # We force the APIs to specify instanceOf since ArrayBufferView isn't an
      # instantiable type, therefore we don't specify isInstanceOf here.
    elif self.typeref == 'FileEntry':
      properties['type'] = 'object'
      properties['isInstanceOf'] = 'FileEntry'
      if 'additionalProperties' not in properties:
        properties['additionalProperties'] = OrderedDict()
      properties['additionalProperties']['type'] = 'any'
    elif self.parent.GetPropertyLocal('Union'):
      properties['choices'] = [
          Typeref(node.GetProperty('TYPEREF'), node,
                  OrderedDict()).process(callbacks)
          for node in self.parent.GetChildren() if node.cls == 'Option'
      ]
    elif self.typeref is None:
      properties['type'] = 'function'
    else:
      if self.typeref in callbacks:
        # Do not override name and description if they are already specified.
        name = properties.get('name', None)
        description = properties.get('description', None)
        properties.update(callbacks[self.typeref])
        if description is not None:
          properties['description'] = description
        if name is not None:
          properties['name'] = name
      else:
        properties['$ref'] = self.typeref
    return result


class Enum(object):
  '''
  Given an IDL Enum node, converts into a Python dictionary that the JSON
  schema compiler expects to see.
  '''

  def __init__(self, enum_node):
    self.node = enum_node
    self.description = ''

  def process(self):
    enum = []
    for node in self.node.GetChildren():
      if node.cls == 'EnumItem':
        enum_value = {'name': node.GetName()}
        if node.GetProperty('nodoc'):
          enum_value['nodoc'] = True
        for child in node.GetChildren():
          if child.cls == 'Comment':
            enum_value['description'] = ProcessComment(child.GetName())[0]
          else:
            raise ValueError('Did not process %s %s' % (child.cls, child))
        enum.append(enum_value)
      elif node.cls == 'Comment':
        self.description = ProcessComment(node.GetName())[0]
      else:
        sys.exit('Did not process %s %s' % (node.cls, node))
    result = {
        'id': self.node.GetName(),
        'description': self.description,
        'type': 'string',
        'enum': enum
    }
    for property_name in ['cpp_enum_prefix_override', 'nodoc']:
      if self.node.GetProperty(property_name):
        result[property_name] = self.node.GetProperty(property_name)
    if self.node.GetProperty('deprecated'):
      result['deprecated'] = self.node.GetProperty('deprecated')
    return result


class Namespace(object):
  '''
  Given an IDLNode representing an IDL namespace, converts into a Python
  dictionary that the JSON schema compiler expects to see.
  '''

  def __init__(self,
               namespace_node,
               description,
               nodoc=False,
               platforms=None,
               compiler_options=None,
               deprecated=None):
    self.namespace = namespace_node
    self.nodoc = nodoc
    self.platforms = platforms
    self.compiler_options = compiler_options
    self.events = []
    self.functions = []
    self.properties = OrderedDict()
    self.manifest_keys = None
    self.types = []
    self.callbacks = OrderedDict()
    self.description = description.strip().replace('\n', '')
    self.deprecated = deprecated

  def process(self):
    for node in self.namespace.GetChildren():
      if node.cls == 'Dictionary' and node.GetName() == 'ManifestKeys':
        self.manifest_keys = Dictionary(node).process(
            self.callbacks)['properties']
      elif node.cls == 'Dictionary':
        self.types.append(Dictionary(node).process(self.callbacks))
      elif node.cls == 'Callback':
        k, v = Member(node).process(self.callbacks)
        self.callbacks[k] = v
      elif node.cls == 'Interface' and node.GetName() == 'Functions':
        self.functions = self.process_interface(node)
      elif node.cls == 'Interface' and node.GetName() == 'Events':
        self.events = self.process_interface(node)
      elif node.cls == 'Interface' and node.GetName() == 'Properties':
        properties_as_list = self.process_interface(
            node, functions_are_properties=True)
        for prop in properties_as_list:
          # Properties are given as key-value pairs, but IDL will parse
          # it as a list. Convert back to key-value pairs.
          prop_name = prop.pop('name')
          assert not prop_name in self.properties, (
              'Property "%s" cannot be specified more than once.' % prop_name)
          self.properties[prop_name] = prop
      elif node.cls == 'Enum':
        self.types.append(Enum(node).process())
      else:
        sys.exit('Did not process %s %s' % (node.cls, node))
    compiler_options = self.compiler_options or {}
    return {
        'namespace': self.namespace.GetName(),
        'description': self.description,
        'nodoc': self.nodoc,
        'types': self.types,
        'functions': self.functions,
        'properties': self.properties,
        'manifest_keys': self.manifest_keys,
        'events': self.events,
        'platforms': self.platforms,
        'compiler_options': compiler_options,
        'deprecated': self.deprecated,
    }

  def process_interface(self, node, functions_are_properties=False):
    members = []
    # Callspec definitions for Functions and Properties with an asynchronous
    # return are defined with a trailing callback, but during parsing we move
    # the details to a returns_async field. We only want to do this for Function
    # and Property definitions, not for Event or IDL callback definitions.
    # TODO(tjudkins): Once IDL definitions are changed to describe returning
    # promises, we can condition on that rather than this special casing here.
    use_returns_async = node.GetName() in ['Functions', 'Properties']
    for member in node.GetChildren():
      if member.cls == 'Member':
        _, properties = Member(member).process(
            self.callbacks,
            functions_are_properties=functions_are_properties,
            use_returns_async=use_returns_async)
        members.append(properties)
    return members


class IDLSchema(object):
  '''
  Given a list of IDLNodes and IDLAttributes, converts into a Python list
  of api_defs that the JSON schema compiler expects to see.
  '''

  def __init__(self, idl):
    self.idl = idl

  def process(self):
    namespaces = []
    nodoc = False
    description = None
    platforms = None
    compiler_options = {}
    deprecated = None
    for node in self.idl:
      if node.cls == 'Namespace':
        if not description:
          # TODO(kalman): Go back to throwing an error here.
          print('%s must have a namespace-level comment. This will '
                'appear on the API summary page.' % node.GetName())
          description = ''
        namespace = Namespace(node,
                              description,
                              nodoc,
                              platforms=platforms,
                              compiler_options=compiler_options or None,
                              deprecated=deprecated)
        namespaces.append(namespace.process())
        nodoc = False
        platforms = None
        compiler_options = None
      elif node.cls == 'Copyright':
        continue
      elif node.cls == 'Comment':
        description = node.GetName()
      elif node.cls == 'ExtAttribute':
        if node.name == 'nodoc':
          nodoc = bool(node.value)
        elif node.name == 'platforms':
          platforms = list(node.value)
        elif node.name == 'implemented_in':
          compiler_options['implemented_in'] = node.value
        elif node.name == 'generate_error_messages':
          compiler_options['generate_error_messages'] = True
        elif node.name == 'deprecated':
          deprecated = str(node.value)
        else:
          continue
      else:
        sys.exit('Did not process %s %s' % (node.cls, node))
    return namespaces


def Load(filename):
  '''
  Given the filename of an IDL file, parses it and returns an equivalent
  Python dictionary in a format that the JSON schema compiler expects to see.
  '''

  with open(filename, 'rb') as handle:
    contents = handle.read().decode('utf-8')

  return Process(contents, filename)


def Process(contents, filename):
  '''
  Processes the contents of a file and returns an equivalent Python dictionary
  in a format that the JSON schema compiler expects to see. (Separate from
  Load primarily for testing purposes.)
  '''

  idl = idl_parser.IDLParser().ParseData(contents, filename)
  idl_schema = IDLSchema(idl)
  return idl_schema.process()


def Main():
  '''
  Dump a json serialization of parse result for the IDL files whose names
  were passed in on the command line.
  '''
  if len(sys.argv) > 1:
    for filename in sys.argv[1:]:
      schema = Load(filename)
      print(json.dumps(schema, indent=2))
  else:
    contents = sys.stdin.read()
    for i, char in enumerate(contents):
      if not char.isascii():
        raise Exception(
            'Non-ascii character "%s" (ord %d) found at offset %d.' %
            (char, ord(char), i))
    idl = idl_parser.IDLParser().ParseData(contents, '<stdin>')
    schema = IDLSchema(idl).process()
    print(json.dumps(schema, indent=2))


if __name__ == '__main__':
  Main()
