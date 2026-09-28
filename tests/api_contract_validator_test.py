#!/usr/bin/env python3
"""Focused behavioral tests for validate-api-contracts.py."""

import importlib.util
import json
import sys
import tempfile
from pathlib import Path


def load_validator(root):
    path = root / "scripts" / "validate-api-contracts.py"
    spec = importlib.util.spec_from_file_location("validate_api_contracts", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_fixture(root, wrapper_symbol="wrapper_call", registration="register_api",
                  export_name="callApi"):
    (root / "api" / "contracts").mkdir(parents=True)
    (root / "src").mkdir()
    (root / "src" / "wrapper.h").write_text(
        "bool wrapper_call(bool enabled);\n", encoding="utf-8")
    (root / "src" / "binding.c").write_text(
        'void register_api(void) { wrapper_call(true); }\n'
        'static const char *exported = "callApi";\n', encoding="utf-8")
    contract = {
        "version": 1,
        "subsystem": "fixture",
        "error_convention": "never_fails",
        "wrapper_header": "src/wrapper.h",
        "runtimes": {
            "javascript": {
                "source": "src/binding.c",
                "registration_symbol": registration,
                "namespace": "sys.fixture",
                "platforms": ["desktop"],
            }
        },
        "operations": [{
            "name": "call",
            "wrapper_symbol": wrapper_symbol,
            "signature": "(bool enabled) -> bool",
            "exports": {"javascript": export_name},
        }],
    }
    path = root / "api" / "contracts" / "fixture.json"
    path.write_text(json.dumps(contract), encoding="utf-8")
    return path, contract


def validate(module, root, path, contract):
    errors = []
    summary = module.validate_contract(root, path, contract, errors)
    return summary, errors


def main():
    source_root = Path(sys.argv[1]).resolve()
    module = load_validator(source_root)
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        path, contract = write_fixture(root)
        summary, errors = validate(module, root, path, contract)
        assert not errors, errors
        assert summary["runtimes"]["javascript"]["exportCount"] == 1

        broken = json.loads(json.dumps(contract))
        broken["operations"][0]["wrapper_symbol"] = "missing_wrapper"
        _, errors = validate(module, root, path, broken)
        assert any("wrapper symbol not declared" in error for error in errors), errors

        broken = json.loads(json.dumps(contract))
        broken["runtimes"]["javascript"]["registration_symbol"] = "missing_registration"
        _, errors = validate(module, root, path, broken)
        assert any("registration symbol not found" in error for error in errors), errors

        broken = json.loads(json.dumps(contract))
        broken["operations"][0]["exports"]["javascript"] = "missingExport"
        _, errors = validate(module, root, path, broken)
        assert any("export token not found" in error for error in errors), errors

        structured = json.loads(json.dumps(contract))
        structured["error_convention"] = {
            "javascript": {
                "kind": "documented_sentinel_on_failure",
                "details": "Returns false when the operation cannot complete.",
            },
        }
        structured["runtimes"]["javascript"]["namespace"] = ["sys.fixture"]
        structured["runtimes"]["javascript"]["export_extractor"] = "quickjs_cfunc"
        structured["operations"][0]["exports"]["javascript"] = {
            "name": "callApi",
            "namespace": "sys.fixture",
            "signature": "(bool enabled) -> bool",
        }
        (root / "src" / "binding.c").write_text(
            'void register_api(void) { wrapper_call(true); }\n'
            'static void binding_call(void) {}\n'
            'static const int funcs[] = {\n'
            '    JS_CFUNC_DEF("callApi", 1, binding_call),\n'
            '    JS_CFUNC_DEF("otherApi", 0, binding_call),\n'
            '};\n', encoding="utf-8")
        _, errors = validate(module, root, path, structured)
        assert any("uncontracted exports: otherApi" in error for error in errors), errors

        structured["operations"].append({
            "name": "other",
            "signature": "() -> bool",
            "binding_symbols": {"javascript": "binding_call"},
            "exports": {
                "javascript": {
                    "name": "otherApi",
                    "namespace": "sys.fixture",
                }
            },
        })
        summary, errors = validate(module, root, path, structured)
        assert not errors, errors
        assert summary["runtimes"]["javascript"]["exportCount"] == 2

        source_token = json.loads(json.dumps(contract))
        source_token["operations"][0]["exports"]["javascript"] = {
            "name": "publicSyntax",
            "source_token": "callApi",
        }
        summary, errors = validate(module, root, path, source_token)
        assert not errors, errors
        assert summary["runtimes"]["javascript"]["exportCount"] == 1

        broken = json.loads(json.dumps(source_token))
        broken["operations"][0]["exports"]["javascript"]["source_token"] = (
            "missing_source_token"
        )
        _, errors = validate(module, root, path, broken)
        assert any("export token not found: missing_source_token" in error
                   for error in errors), errors

        broken = json.loads(json.dumps(source_token))
        broken["operations"][0]["exports"]["javascript"]["source_token"] = None
        _, errors = validate(module, root, path, broken)
        assert any("source_token: expected a non-empty string" in error
               for error in errors), errors

        scoped = json.loads(json.dumps(contract))
        scoped["runtimes"]["javascript"]["export_extractor"] = "quickjs_cfunc"
        scoped["runtimes"]["javascript"]["export_scopes"] = ["fixture_funcs"]
        (root / "src" / "binding.c").write_text(
            'void register_api(void) { wrapper_call(true); }\n'
            'static void binding_call(void) {}\n'
            'static const int fixture_funcs[] = {\n'
            '    JS_CFUNC_DEF("callApi", 1, binding_call),\n'
            '};\n'
            'static const int unrelated_funcs[] = {\n'
            '    JS_CFUNC_DEF("unrelatedApi", 0, binding_call),\n'
            '};\n', encoding="utf-8")
        summary, errors = validate(module, root, path, scoped)
        assert not errors, errors
        assert summary["runtimes"]["javascript"]["exportCount"] == 1

        broken = json.loads(json.dumps(scoped))
        broken["runtimes"]["javascript"]["export_scopes"] = ["missing_funcs"]
        _, errors = validate(module, root, path, broken)
        assert any("initializer not found: missing_funcs" in error
                   for error in errors), errors

        constants = json.loads(json.dumps(structured))
        constants["runtimes"]["javascript"]["export_extractor"] = [
            "quickjs_cfunc",
            "quickjs_int_property",
        ]
        constants["runtimes"]["javascript"]["constant_export_scope"] = "add_constants"
        constants["constants"] = [{
            "name": "ANSWER",
            "value": 42,
            "exports": {"javascript": "ANSWER"},
        }]
        (root / "src" / "binding.c").write_text(
            'void register_api(void) { wrapper_call(true); }\n'
            'static void binding_call(void) {}\n'
            'static const int funcs[] = {\n'
            '    JS_CFUNC_DEF("callApi", 1, binding_call),\n'
            '    JS_CFUNC_DEF("otherApi", 0, binding_call),\n'
            '};\n'
            'void add_constants(void *ctx, int object) {\n'
            '    JS_SetPropertyStr(ctx, object, "ANSWER", JS_NewInt32(ctx, 42));\n'
            '}\n'
            'void add_metadata(void *ctx, int object) {\n'
            '    JS_SetPropertyStr(ctx, object, "id", JS_NewInt32(ctx, 7));\n'
            '}\n', encoding="utf-8")
        summary, errors = validate(module, root, path, constants)
        assert not errors, errors
        assert summary["runtimes"]["javascript"]["exportCount"] == 3
        assert summary["runtimes"]["javascript"]["constantExportCount"] == 1

        broken = json.loads(json.dumps(constants))
        broken["runtimes"]["javascript"]["constant_export_scope"] = "missing_scope"
        _, errors = validate(module, root, path, broken)
        assert any("function body not found: missing_scope" in error for error in errors), errors

        extended = json.loads(json.dumps(structured))
        extended["wrapper_headers"] = [extended.pop("wrapper_header")]
        extended["constants"] = [{
            "name": "CALL_MODE",
            "value": 1,
            "exports": {"javascript": "CALL_MODE"},
        }]
        extended["capability_relationships"] = [{
            "subsystem": "capabilities",
            "operation": "udp",
            "kind": "required",
            "required_for_registration": False,
            "runtimes": ["javascript"],
            "applies_to": ["call"],
            "unavailable_behavior": "The call export remains registered and returns false.",
        }]
        extended["platform_differences"] = {
            "web": {
                "source": "src/browser.c",
                "details": ["The web implementation returns false without raw sockets."],
            }
        }
        (root / "src" / "browser.c").write_text(
            "const int CALL_MODE = 1;\n", encoding="utf-8")
        with (root / "src" / "binding.c").open("a", encoding="utf-8") as binding:
            binding.write('static const int CALL_MODE = 1;\n')
        summary, errors = validate(module, root, path, extended)
        assert not errors, errors
        assert summary["constantCount"] == 1
        assert summary["runtimes"]["javascript"]["exportCount"] == 3
        assert summary["runtimes"]["javascript"]["constantExportCount"] == 1

        broken = json.loads(json.dumps(extended))
        broken["capability_relationships"][0]["applies_to"] = ["missing"]
        _, errors = validate(module, root, path, broken)
        assert any("unknown operation: missing" in error for error in errors), errors

        source_gated = json.loads(json.dumps(structured))
        source_gated["availability_gates"] = [{
            "kind": "runtime_dependency",
            "registration_effect": "required",
            "runtimes": ["javascript"],
            "platforms": ["desktop"],
            "source": "src/binding.c",
            "source_symbols": ["register_api", "wrapper_call"],
            "behavior": "Registration succeeds only when the dependency is available.",
        }]
        summary, errors = validate(module, root, path, source_gated)
        assert not errors, errors
        assert len(summary["availabilityGates"]) == 1

        source_gated["state_ownership"] = {
            "javascript": "The closure owns one fixture state.",
        }
        source_gated["typescript"] = {
            "interface": "SysFixture",
            "kind": "methods",
            "description": "Fixture methods generated from this contract.",
        }
        summary, errors = validate(module, root, path, source_gated)
        assert not errors, errors
        assert summary["stateOwnership"]["javascript"].startswith("The closure")
        assert summary["typescript"]["interface"] == "SysFixture"

        broken = json.loads(json.dumps(source_gated))
        broken["typescript"]["kind"] = "full_binding_generator"
        _, errors = validate(module, root, path, broken)
        assert any("unsupported TypeScript generation kind" in error
                   for error in errors), errors

        broken = json.loads(json.dumps(source_gated))
        broken["availability_gates"][0]["source_symbols"].append("missing_gate")
        _, errors = validate(module, root, path, broken)
        assert any("source symbol not found: missing_gate" in error for error in errors), errors

        broken = json.loads(json.dumps(source_gated))
        broken["availability_gates"][0]["platforms"] = ["web"]
        _, errors = validate(module, root, path, broken)
        assert any("platform is not supported by runtime javascript" in error for error in errors), errors

        browser_exports = json.loads(json.dumps(contract))
        browser_exports["runtimes"]["javascript"]["export_extractor"] = "javascript_object_key"
        browser_exports["runtimes"]["javascript"]["export_prefixes"] = ["fixture_"]
        browser_exports["operations"][0]["exports"]["javascript"] = "fixture_call"
        (root / "src" / "binding.c").write_text(
            "void register_api(void) { wrapper_call(true); }\n"
            "const int env = {\n"
            "    fixture_call: 1,\n"
            "    unrelated_call: 2,\n"
            "};\n",
            encoding="utf-8",
        )
        summary, errors = validate(module, root, path, browser_exports)
        assert not errors, errors
        assert summary["runtimes"]["javascript"]["exportCount"] == 1

        dynamic_source = (
            'static const int funcs[] = { JS_CFUNC_DEF("native", 0, call) };\n'
            'JSValue exported = JS_NewCFunction(ctx, call, "exported", 0);\n'
            'JS_SetPropertyStr(ctx, one, "exported", JS_DupValue(ctx, exported));\n'
            'JS_SetPropertyStr(ctx, two, "exported", exported);\n'
            'JS_SetPropertyStr(ctx, response, "text",\n'
            '                  JS_NewCFunction(ctx, call, "text", 0));\n'
            'JS_SetPropertyStr(ctx, response, "json",\n'
            '                  JS_NewCFunctionMagic(ctx, call_magic, "json", 0, JS_CFUNC_generic_magic, 7));\n'
            'JSValue captured = JS_NewCFunctionData(ctx, call_data, 2, 0, 1, &data);\n'
            'JS_SetPropertyStr(ctx, one, "captured", JS_DupValue(ctx, captured));\n'
            '"n.native = function() {}; n.added = function() {};"\n'
            'static const luaL_Reg lua_funcs[] = { {"native", l_native} };\n'
            'lua_setglobal(L, "global_call");\n'
            '"function n.native() end function n.added() end"\n'
        )
        assert module.extract_exports(dynamic_source, "quickjs_function_property") == [
            "text", "json", "exported", "exported", "captured"
        ]
        function_data_source = (
            'static const JsDbFunction functions[] = {\n'
            '    {"open", 1, js_open},\n'
            '    {"query", 2, js_query},\n'
            '};\n'
        )
        assert module.extract_exports(
            function_data_source, "quickjs_function_data") == ["open", "query"]
        midi_function_data_source = (
            'static const JsMidiFunction js_midi_funcs[] = {\n'
            '    {"openInput", 2, js_midi_open_input},\n'
            '    {"sessionSend", 4, js_midi_session_send},\n'
            '};\n'
        )
        assert module.extract_exports(
            midi_function_data_source, "quickjs_function_data") == [
                "openInput", "sessionSend"
            ]
        assert module.extract_exports(
            function_data_source +
            '"n.query = function() {}; n.added = function() {};"\n',
            "quickjs_shim_addition") == ["added"]
        wasmtime_state_source = (
            'define_sqlite_func(linker, state, "db_open", host_open, params, 2, results, 1);\n'
            'define_sqlite_func(linker, state, "db_close", host_close, params, 1, NULL, 0);\n'
        )
        assert module.extract_exports(
            wasmtime_state_source, "wasmtime_host") == ["db_open", "db_close"]
        assert module.extract_exports(dynamic_source, "quickjs_shim_addition") == ["added"]
        assert module.extract_exports(dynamic_source, "lua_global_function") == ["global_call"]
        assert module.extract_exports(dynamic_source, "lua_shim_addition") == ["added"]
        lua_field_source = (
            'lua_pushcfunction(L, l_log);\n'
            'lua_setfield(L, -2, "log");\n'
        )
        assert module.extract_exports(
            lua_field_source, "lua_function_field") == ["log"]
        canvas_wasmtime_source = (
            'define_host_function(ctx->linker, "env", "canvas_clear", '
            'host_canvas_clear, params, 1, NULL, 0);\n'
        )
        assert module.extract_exports(
            canvas_wasmtime_source, "wasmtime_host") == ["canvas_clear"]

        (root / "src" / "binding.c").write_text(
            'void register_api(void) { wrapper_call(true); }\n'
            'static void binding_call(void) {}\n'
            'static const int funcs[] = {\n'
            '    JS_CFUNC_DEF("callApi", 1, binding_call),\n'
            '    JS_CFUNC_DEF("otherApi", 0, binding_call),\n'
            '};\n',
            encoding="utf-8",
        )

        gated = json.loads(json.dumps(structured))
        gated["capability_gate"] = {
            "subsystem": "capabilities",
            "operation": "sensors",
            "kind": "availability_probe",
            "required_for_registration": False,
            "unavailable_behavior": "The API remains registered and returns false.",
        }
        gate_errors = []
        module.validate_capability_gates(root, [(path, gated)], gate_errors)
        assert any("unknown subsystem: capabilities" in error for error in gate_errors), gate_errors

        duplicate_owner = json.loads(json.dumps(contract))
        duplicate_owner["subsystem"] = "other_fixture"
        duplicate_owner["operations"][0]["name"] = "other_call"
        ownership_errors = []
        module.validate_wrapper_ownership(
            root,
            [(path, contract),
             (root / "api" / "contracts" / "other.json", duplicate_owner)],
            ownership_errors,
        )
        assert any("wrapper symbol already owned by fixture.call: wrapper_call"
                   in error for error in ownership_errors), ownership_errors

        unsupported = json.loads(json.dumps(structured))
        unsupported["unsupported_runtimes"] = {
            "browser_wasm": {
                "source": "src/browser.c",
                "registration_symbol": "start_browser_wasm",
                "platforms": ["web"],
                "reason": "The browser env must not expose fixture imports.",
                "absent_export_prefixes": ["fixture_"],
            }
        }
        (root / "src" / "browser.c").write_text(
            "void start_browser_wasm(void) {}\n"
            "const char *env = \"fixture_call\";\n",
            encoding="utf-8",
        )
        _, errors = validate(module, root, path, unsupported)
        assert not errors, errors
        (root / "src" / "browser.c").write_text(
            "void start_browser_wasm(void) {}\n"
            "const int env = {\n"
            "    fixture_call: 1,\n"
            "};\n",
            encoding="utf-8",
        )
        _, errors = validate(module, root, path, unsupported)
        assert any("unsupported runtime exports found" in error for error in errors), errors

    print("API contract validator tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())