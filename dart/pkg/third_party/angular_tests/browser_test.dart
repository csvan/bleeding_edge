/// auto-generated by update_angular.sh

// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library angular_browser_tests;

import 'package:angular/mock/module.dart';
import 'package:unittest/html_individual_config.dart';
import 'package:unittest/unittest.dart';
import '../../../third_party/pkg/angular/test/angular_spec.dart' as test_0;
import '../../../third_party/pkg/angular/test/bootstrap_spec.dart' as test_1;
import '../../../third_party/pkg/angular/test/change_detection/watch_group_spec.dart' as test_2;
import '../../../third_party/pkg/angular/test/change_detection/dirty_checking_change_detector_spec.dart' as test_3;
import '../../../third_party/pkg/angular/test/core/zone_spec.dart' as test_4;
import '../../../third_party/pkg/angular/test/core/registry_spec.dart' as test_5;
import '../../../third_party/pkg/angular/test/core/parser/static_parser_spec.dart' as test_6;
import '../../../third_party/pkg/angular/test/core/parser/parser_spec.dart' as test_7;
import '../../../third_party/pkg/angular/test/core/parser/generated_getter_setter_spec.dart' as test_8;
import '../../../third_party/pkg/angular/test/core/parser/lexer_spec.dart' as test_9;
import '../../../third_party/pkg/angular/test/core/core_directive_spec.dart' as test_10;
import '../../../third_party/pkg/angular/test/core/scope_spec.dart' as test_11;
import '../../../third_party/pkg/angular/test/core/interpolate_spec.dart' as test_12;
import '../../../third_party/pkg/angular/test/core/cache_spec.dart' as test_13;
import '../../../third_party/pkg/angular/test/core/templateurl_spec.dart' as test_14;
import '../../../third_party/pkg/angular/test/core_dom/node_cursor_spec.dart' as test_15;
import '../../../third_party/pkg/angular/test/core_dom/shadow_root_options_spec.dart' as test_16;
import '../../../third_party/pkg/angular/test/core_dom/block_spec.dart' as test_17;
import '../../../third_party/pkg/angular/test/core_dom/selector_spec.dart' as test_18;
import '../../../third_party/pkg/angular/test/core_dom/compiler_spec.dart' as test_19;
import '../../../third_party/pkg/angular/test/core_dom/ng_mustache_spec.dart' as test_20;
import '../../../third_party/pkg/angular/test/core_dom/http_spec.dart' as test_21;
import '../../../third_party/pkg/angular/test/core_dom/cookies_spec.dart' as test_22;
import '../../../third_party/pkg/angular/test/core_dom/directive_spec.dart' as test_23;
import '../../../third_party/pkg/angular/test/directive/ng_repeat_spec.dart' as test_24;
import '../../../third_party/pkg/angular/test/directive/ng_show_hide_spec.dart' as test_25;
import '../../../third_party/pkg/angular/test/directive/ng_a_spec.dart' as test_26;
import '../../../third_party/pkg/angular/test/directive/ng_pluralize_spec.dart' as test_27;
import '../../../third_party/pkg/angular/test/directive/ng_template_spec.dart' as test_28;
import '../../../third_party/pkg/angular/test/directive/input_select_spec.dart' as test_29;
import '../../../third_party/pkg/angular/test/directive/ng_form_spec.dart' as test_30;
import '../../../third_party/pkg/angular/test/directive/ng_switch_spec.dart' as test_31;
import '../../../third_party/pkg/angular/test/directive/ng_bind_html_spec.dart' as test_32;
import '../../../third_party/pkg/angular/test/directive/ng_non_bindable_spec.dart' as test_33;
import '../../../third_party/pkg/angular/test/directive/ng_include_spec.dart' as test_34;
import '../../../third_party/pkg/angular/test/directive/ng_style_spec.dart' as test_35;
import '../../../third_party/pkg/angular/test/directive/ng_cloak_spec.dart' as test_36;
import '../../../third_party/pkg/angular/test/directive/ng_model_spec.dart' as test_37;
import '../../../third_party/pkg/angular/test/directive/ng_src_boolean_spec.dart' as test_38;
import '../../../third_party/pkg/angular/test/directive/ng_bind_template_spec.dart' as test_39;
import '../../../third_party/pkg/angular/test/directive/ng_events_spec.dart' as test_40;
import '../../../third_party/pkg/angular/test/directive/ng_class_spec.dart' as test_41;
import '../../../third_party/pkg/angular/test/directive/ng_if_spec.dart' as test_42;
import '../../../third_party/pkg/angular/test/directive/ng_model_validators_spec.dart' as test_43;
import '../../../third_party/pkg/angular/test/directive/ng_bind_spec.dart' as test_44;
import '../../../third_party/pkg/angular/test/filter/currency_spec.dart' as test_45;
import '../../../third_party/pkg/angular/test/filter/lowercase_spec.dart' as test_46;
import '../../../third_party/pkg/angular/test/filter/date_spec.dart' as test_47;
import '../../../third_party/pkg/angular/test/filter/order_by_spec.dart' as test_48;
import '../../../third_party/pkg/angular/test/filter/limit_to_spec.dart' as test_49;
import '../../../third_party/pkg/angular/test/filter/json_spec.dart' as test_50;
import '../../../third_party/pkg/angular/test/filter/uppercase_spec.dart' as test_51;
import '../../../third_party/pkg/angular/test/filter/filter_spec.dart' as test_52;
import '../../../third_party/pkg/angular/test/filter/number_spec.dart' as test_53;
import '../../../third_party/pkg/angular/test/introspection_spec.dart' as test_54;
import '../../../third_party/pkg/angular/test/mock/zone_spec.dart' as test_55;
import '../../../third_party/pkg/angular/test/mock/test_bed_spec.dart' as test_56;
import '../../../third_party/pkg/angular/test/mock/http_backend_spec.dart' as test_57;
import '../../../third_party/pkg/angular/test/routing/ng_bind_route_spec.dart' as test_58;
import '../../../third_party/pkg/angular/test/routing/routing_spec.dart' as test_59;
import '../../../third_party/pkg/angular/test/routing/ng_view_spec.dart' as test_60;
import '../../../third_party/pkg/angular/test/_specs_spec.dart' as test_61;

main() {
  useHtmlIndividualConfiguration();

  setUp(() {
    setUpInjector();
  });

  group('angular', () {
    test_0.main();
  });

  group('bootstrap', () {
    test_1.main();
  });

  group('change_detection/watch_group', () {
    test_2.main();
  });

  group('change_detection/dirty_checking_change_detector', () {
    test_3.main();
  });

  group('core/zone', () {
    test_4.main();
  });

  group('core/registry', () {
    test_5.main();
  });

  group('core/parser/static_parser', () {
    test_6.main();
  });

  group('core/parser/parser', () {
    test_7.main();
  });

  group('core/parser/generated_getter_setter', () {
    test_8.main();
  });

  group('core/parser/lexer', () {
    test_9.main();
  });

  group('core/core_directive', () {
    test_10.main();
  });

  group('core/scope', () {
    test_11.main();
  });

  group('core/interpolate', () {
    test_12.main();
  });

  group('core/cache', () {
    test_13.main();
  });

  group('core/templateurl', () {
    test_14.main();
  });

  group('core_dom/node_cursor', () {
    test_15.main();
  });

  group('core_dom/shadow_root_options', () {
    test_16.main();
  });

  group('core_dom/block', () {
    test_17.main();
  });

  group('core_dom/selector', () {
    test_18.main();
  });

  group('core_dom/compiler', () {
    test_19.main();
  });

  group('core_dom/ng_mustache', () {
    test_20.main();
  });

  group('core_dom/http', () {
    test_21.main();
  });

  group('core_dom/cookies', () {
    test_22.main();
  });

  group('core_dom/directive', () {
    test_23.main();
  });

  group('directive/ng_repeat', () {
    test_24.main();
  });

  group('directive/ng_show_hide', () {
    test_25.main();
  });

  group('directive/ng_a', () {
    test_26.main();
  });

  group('directive/ng_pluralize', () {
    test_27.main();
  });

  group('directive/ng_template', () {
    test_28.main();
  });

  group('directive/input_select', () {
    test_29.main();
  });

  group('directive/ng_form', () {
    test_30.main();
  });

  group('directive/ng_switch', () {
    test_31.main();
  });

  group('directive/ng_bind_html', () {
    test_32.main();
  });

  group('directive/ng_non_bindable', () {
    test_33.main();
  });

  group('directive/ng_include', () {
    test_34.main();
  });

  group('directive/ng_style', () {
    test_35.main();
  });

  group('directive/ng_cloak', () {
    test_36.main();
  });

  group('directive/ng_model', () {
    test_37.main();
  });

  group('directive/ng_src_boolean', () {
    test_38.main();
  });

  group('directive/ng_bind_template', () {
    test_39.main();
  });

  group('directive/ng_events', () {
    test_40.main();
  });

  group('directive/ng_class', () {
    test_41.main();
  });

  group('directive/ng_if', () {
    test_42.main();
  });

  group('directive/ng_model_validators', () {
    test_43.main();
  });

  group('directive/ng_bind', () {
    test_44.main();
  });

  group('filter/currency', () {
    test_45.main();
  });

  group('filter/lowercase', () {
    test_46.main();
  });

  group('filter/date', () {
    test_47.main();
  });

  group('filter/order_by', () {
    test_48.main();
  });

  group('filter/limit_to', () {
    test_49.main();
  });

  group('filter/json', () {
    test_50.main();
  });

  group('filter/uppercase', () {
    test_51.main();
  });

  group('filter/filter', () {
    test_52.main();
  });

  group('filter/number', () {
    test_53.main();
  });

  group('introspection', () {
    test_54.main();
  });

  group('mock/zone', () {
    test_55.main();
  });

  group('mock/test_bed', () {
    test_56.main();
  });

  group('mock/http_backend', () {
    test_57.main();
  });

  group('routing/ng_bind_route', () {
    test_58.main();
  });

  group('routing/routing', () {
    test_59.main();
  });

  group('routing/ng_view', () {
    test_60.main();
  });

  group('_specs', () {
    test_61.main();
  });
}
