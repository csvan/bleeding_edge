// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library unittestTest;
import 'dart:isolate';
import 'dart:async';
import 'package:unittest/unittest.dart';

part 'unittest_test_utils.dart';

var testFunction = (_) {
  test('protectAsync0', () {
    var protected = protectAsync0(() {
      throw new StateError('error during protectAsync0');
    });
    new Future(protected);
  });

  test('protectAsync1', () {
    var protected = protectAsync1((arg) {
      throw new StateError('error during protectAsync1: $arg');
    });
    new Future(() => protected('one arg'));
  });

  test('protectAsync2', () {
    var protected = protectAsync2((arg1, arg2) {
      throw new StateError('error during protectAsync2: $arg1, $arg2');
    });
    new Future(() => protected('arg1', 'arg2'));
  });

  test('throw away 1', () {
    return new Future(() {});
  });
};

var expected = '1:0:3:4:0:::null:'
  'protectAsync0:Callback called (1) after test case protectAsync0 has already been marked as pass.:'
  'protectAsync1:Callback called (1) after test case protectAsync1 has already been marked as pass.:'
  'protectAsync2:Callback called (1) after test case protectAsync2 has already been marked as pass.:'
  'throw away 1:';