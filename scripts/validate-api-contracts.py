#!/usr/bin/env python3
"""Validate source-backed managed API contracts and emit a support matrix."""

import argparse
from collections import Counter
import json
import re
import sys
from pathlib import Path


PLATFORMS = {"desktop", "android", "web"}
RUNTIMES = {"javascript", "lua", "wasmtime", "browser_wasm"}
ERROR_CONVENTIONS = {
    "never_fails",
    "programmer_error_on_invalid_buffer",
    "transient_returns_false",
    "throw_programmer_error_return_falsy_transient",
    "throw_programmer_error_report_async_failure",
    "throws_on_invalid_arguments_or_operation_failure",
    "documented_sentinel_on_failure",
    "unavailable_returns_false_or_null",
    "unavailable_returns_false_or_zero",
}
EXPORT_EXTRACTORS = {
    "javascript_object_key",
    "quickjs_cfunc",
    "quickjs_function_data",
    "quickjs_function_property",
    "quickjs_int_property",
    "quickjs_shim_addition",
    "lua_global_function",
    "lua_function_field",
    "lua_reg",
    "lua_integer_field",
    "lua_shim_addition",
    "wasmtime_host",
}
CONSTANT_EXPORT_EXTRACTORS = {"quickjs_int_property", "lua_integer_field"}
CAPABILITY_GATE_KINDS = {"availability_probe", "required"}
AVAILABILITY_GATE_KINDS = {"compile_time", "application_metadata", "runtime_dependency"}
REGISTRATION_EFFECTS = {"required", "context_only"}
TYPESCRIPT_KINDS = {"methods", "capability_entries"}


def load_json(path, errors):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"{path}: {exc}")
        return None


def require_string(value, label, errors):
    if not isinstance(value, str) or not value:
        errors.append(f"{label}: expected a non-empty string")
        return False
    return True


def symbol_present(source, symbol):
    return re.search(rf"\b{re.escape(symbol)}\b", source) is not None


def extract_c_function_body(source, symbol):
    match = re.search(
        rf"\b{re.escape(symbol)}\s*\([^;{{}}]*\)\s*\{{",
        source,
        re.DOTALL,
    )
    if not match:
        return None

    start = match.end() - 1
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    return None


def extract_c_initializer_body(source, symbol):
    match = re.search(
        rf"\b{re.escape(symbol)}\b\s*(?:\[[^\]]*\])?\s*=\s*\{{",
        source,
    )
    if not match:
        return None

    start = match.end() - 1
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    return None


def validate_platforms(value, label, errors):
    if not isinstance(value, list) or not value or any(p not in PLATFORMS for p in value):
        errors.append(f"{label}: expected known platform names")
        return []
    if len(value) != len(set(value)):
        errors.append(f"{label}: platform names must be unique")
    return value


def validate_namespaces(value, label, errors):
    values = [value] if isinstance(value, str) else value
    if not isinstance(values, list) or not values:
        errors.append(f"{label}: expected a namespace string or non-empty array")
        return []
    if any(not isinstance(namespace, str) or not namespace for namespace in values):
        errors.append(f"{label}: expected non-empty namespace strings")
        return []
    if len(values) != len(set(values)):
        errors.append(f"{label}: namespaces must be unique")
    return values


def validate_error_convention(value, runtimes, label, errors):
    if isinstance(value, str):
        if value not in ERROR_CONVENTIONS:
            errors.append(f"{label}: unsupported error_convention")
        return
    if not isinstance(value, dict) or not value:
        errors.append(f"{label}: expected a convention or runtime convention object")
        return
    missing = sorted(set(runtimes) - set(value))
    unknown = sorted(set(value) - set(runtimes))
    if missing:
        errors.append(f"{label}: missing runtime conventions: {', '.join(missing)}")
    if unknown:
        errors.append(f"{label}: conventions declared for unknown runtimes: {', '.join(unknown)}")
    for runtime in sorted(value):
        convention = value[runtime]
        if isinstance(convention, str):
            kind = convention
        elif isinstance(convention, dict):
            kind = convention.get("kind")
            require_string(convention.get("details"), f"{label}.{runtime}.details", errors)
        else:
            errors.append(f"{label}.{runtime}: expected a convention or convention object")
            continue
        if kind not in ERROR_CONVENTIONS:
            errors.append(f"{label}.{runtime}: unsupported error_convention")


def validate_availability_gates(root, value, runtimes, label, errors):
    if value is None:
        return []
    if not isinstance(value, list) or not value:
        errors.append(f"{label}: expected a non-empty array")
        return []

    for index, gate in enumerate(value):
        gate_label = f"{label}[{index}]"
        if not isinstance(gate, dict):
            errors.append(f"{gate_label}: expected an object")
            continue
        if gate.get("kind") not in AVAILABILITY_GATE_KINDS:
            errors.append(f"{gate_label}.kind: unsupported availability gate kind")
        if gate.get("registration_effect") not in REGISTRATION_EFFECTS:
            errors.append(f"{gate_label}.registration_effect: unsupported registration effect")

        gate_runtimes = gate.get("runtimes")
        if not isinstance(gate_runtimes, list) or not gate_runtimes:
            errors.append(f"{gate_label}.runtimes: expected a non-empty array")
            gate_runtimes = []
        elif len(gate_runtimes) != len(set(gate_runtimes)):
            errors.append(f"{gate_label}.runtimes: runtime names must be unique")
        for runtime in gate_runtimes:
            if runtime not in RUNTIMES:
                errors.append(f"{gate_label}.runtimes: unknown runtime: {runtime}")
            elif runtime not in runtimes:
                errors.append(f"{gate_label}.runtimes: runtime is not declared: {runtime}")

        gate_platforms = validate_platforms(
            gate.get("platforms"), f"{gate_label}.platforms", errors)
        for runtime in gate_runtimes:
            runtime_contract = runtimes.get(runtime)
            if not isinstance(runtime_contract, dict):
                continue
            supported_platforms = runtime_contract.get("platforms", [])
            if any(platform not in supported_platforms for platform in gate_platforms):
                errors.append(
                    f"{gate_label}.platforms: platform is not supported by runtime {runtime}"
                )

        source_name = gate.get("source")
        source = ""
        if require_string(source_name, f"{gate_label}.source", errors):
            source_path = root / source_name
            if not source_path.is_file():
                errors.append(f"{gate_label}: source does not exist: {source_name}")
            else:
                source = source_path.read_text(encoding="utf-8")

        source_symbols = gate.get("source_symbols")
        if not isinstance(source_symbols, list) or not source_symbols:
            errors.append(f"{gate_label}.source_symbols: expected a non-empty array")
        else:
            if len(source_symbols) != len(set(source_symbols)):
                errors.append(f"{gate_label}.source_symbols: symbols must be unique")
            for symbol_index, symbol in enumerate(source_symbols):
                symbol_label = f"{gate_label}.source_symbols[{symbol_index}]"
                if require_string(symbol, symbol_label, errors):
                    if source and not symbol_present(source, symbol):
                        errors.append(f"{symbol_label}: source symbol not found: {symbol}")

        require_string(gate.get("behavior"), f"{gate_label}.behavior", errors)
    return value


def normalize_export_specs(value, runtime_label, namespaces, platforms, errors):
    values = value if isinstance(value, list) else [value]
    if not values:
        errors.append(f"{runtime_label}: expected at least one export")
        return []
    specs = []
    for index, candidate in enumerate(values):
        label = runtime_label if len(values) == 1 else f"{runtime_label}[{index}]"
        if isinstance(candidate, str):
            name = candidate
            namespace = namespaces[0] if len(namespaces) == 1 else None
            signature = None
            source_token = name
            export_platforms = platforms
        elif isinstance(candidate, dict):
            name = candidate.get("name")
            namespace = candidate.get("namespace")
            signature = candidate.get("signature")
            source_token = candidate.get("source_token", name)
            export_platforms = candidate.get("platforms", platforms)
        else:
            errors.append(f"{label}: expected a string or export object")
            continue
        if not require_string(name, f"{label}.name", errors):
            continue
        if namespace is None and len(namespaces) > 1:
            errors.append(f"{label}.namespace: required when a runtime has multiple namespaces")
        elif namespace is not None:
            if not require_string(namespace, f"{label}.namespace", errors):
                namespace = None
            elif namespace not in namespaces:
                errors.append(f"{label}.namespace: namespace is not declared by the runtime")
        if signature is not None:
            require_string(signature, f"{label}.signature", errors)
        source_token_is_valid = require_string(
            source_token, f"{label}.source_token", errors)
        export_platforms = validate_platforms(export_platforms, f"{label}.platforms", errors)
        if any(platform not in platforms for platform in export_platforms):
            errors.append(f"{label}.platforms: export platform is not supported by the runtime")
        specs.append({
            "name": name,
            "namespace": namespace,
            "signature": signature,
            "source_token": source_token if source_token_is_valid else "",
            "platforms": export_platforms,
        })
    return specs


def extract_exports(source, extractor):
    if extractor == "javascript_object_key":
        return extract_javascript_object_keys(source)
    if extractor == "quickjs_cfunc":
        exports = re.findall(
            r'\bJS_CFUNC_(?:MAGIC_)?DEF\(\s*"([^"]+)"',
            source,
        )
        exports.extend(re.findall(
            r'\{\s*"([^"]+)"\s*,\s*\d+\s*,\s*'
            r'[A-Za-z_][A-Za-z0-9_]*\s*\}',
            source,
        ))
        return exports
    if extractor == "quickjs_function_data":
        return re.findall(
            r'\{\s*"([^"]+)"\s*,\s*\d+\s*,\s*'
            r'[A-Za-z_][A-Za-z0-9_]*\s*\}',
            source,
        )
    if extractor == "quickjs_function_property":
        exports = re.findall(
            r'\bJS_SetPropertyStr\(\s*[^,]+,\s*[^,]+,\s*"([^"]+)"\s*,'
            r'\s*JS_NewCFunction(?:Magic)?\(',
            source,
        )
        function_variables = re.findall(
            r'\bJSValue\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*'
            r'JS_NewCFunction(?:Data)?\([^;]*?\)',
            source,
        )
        for variable in function_variables:
            exports.extend(re.findall(
                r'\bJS_SetPropertyStr\(\s*[^,]+,\s*[^,]+,\s*"([^"]+)"\s*,'
                r'\s*(?:JS_DupValue\(\s*[^,]+,\s*)?'
                + re.escape(variable) + r'\b',
                source,
            ))
        return exports
    if extractor == "quickjs_int_property":
        return re.findall(
            r'\bJS_SetPropertyStr\(\s*[^,]+,\s*[^,]+,\s*"([^"]+)"\s*,'
            r'\s*JS_NewInt32\(',
            source,
        )
    if extractor == "quickjs_shim_addition":
        native = set(extract_exports(source, "quickjs_cfunc"))
        native.update(extract_exports(source, "quickjs_function_data"))
        return [
            name for name in re.findall(
                r'\bn\.([A-Za-z_$][A-Za-z0-9_$]*)\s*=\s*function\b', source)
            if name not in native
        ]
    if extractor == "lua_global_function":
        return re.findall(r'\blua_setglobal\(\s*[^,]+,\s*"([^"]+)"\s*\)', source)
    if extractor == "lua_function_field":
        return re.findall(
            r'\blua_pushcfunction\([^;]*\);\s*'
            r'lua_setfield\(\s*[^,]+,\s*[^,]+,\s*"([^"]+)"\s*\)',
            source,
        )
    if extractor == "lua_reg":
        return re.findall(r'\{\s*"([^"]+)"\s*,\s*l_[A-Za-z_][A-Za-z0-9_]*\s*\}', source)
    if extractor == "lua_integer_field":
        return re.findall(
            r'\blua_pushinteger\([^;]*\);\s*'
            r'lua_setfield\(\s*[^,]+,\s*[^,]+,\s*"([^"]+)"\s*\)',
            source,
        )
    if extractor == "lua_shim_addition":
        native = set(extract_exports(source, "lua_reg"))
        return [
            name for name in re.findall(
                r'\bfunction\s+n\.([A-Za-z_][A-Za-z0-9_]*)\s*\(', source)
            if name not in native
        ]
    if extractor == "wasmtime_host":
        direct = re.findall(
            r'\b(?:[A-Za-z_][A-Za-z0-9_]*_)?define_[A-Za-z_][A-Za-z0-9_]*_func(?:tion)?'
            r'\(\s*(?:[A-Za-z_][A-Za-z0-9_]*->)?linker\s*,\s*'
            r'(?:(?:[A-Za-z_][A-Za-z0-9_]*|"[^"]+")\s*,\s*)?'
            r'"([^"]+)"',
            source,
        )
        indirect = re.findall(
            r'\bsnprintf\(\s*name\s*,\s*sizeof\(name\)\s*,\s*"([^"]+)"',
            source,
        )
        return direct + indirect
    return []


def extract_javascript_object_keys(source):
    return re.findall(
        r"(?m)^\s*(?:['\"])?([A-Za-z_$][A-Za-z0-9_$]*)(?:['\"])?\s*:",
        source,
    )


def validate_capability_reference(value, label, errors):
    if not isinstance(value, dict):
        errors.append(f"{label}: expected an object")
        return
    require_string(value.get("subsystem"), f"{label}.subsystem", errors)
    require_string(value.get("operation"), f"{label}.operation", errors)
    if value.get("kind") not in CAPABILITY_GATE_KINDS:
        errors.append(f"{label}.kind: unsupported gate kind")
    if not isinstance(value.get("required_for_registration"), bool):
        errors.append(f"{label}.required_for_registration: expected a boolean")
    require_string(
        value.get("unavailable_behavior"),
        f"{label}.unavailable_behavior",
        errors,
    )


def validate_platform_differences(root, value, label, errors):
    if value is None:
        return {}
    if not isinstance(value, dict) or not value:
        errors.append(f"{label}: expected a non-empty object")
        return {}
    for platform, difference in sorted(value.items()):
        difference_label = f"{label}.{platform}"
        if platform not in PLATFORMS:
            errors.append(f"{difference_label}: unknown platform")
        if not isinstance(difference, dict):
            errors.append(f"{difference_label}: expected an object")
            continue
        source_name = difference.get("source")
        if require_string(source_name, f"{difference_label}.source", errors):
            source_path = root / source_name
            if not source_path.is_file():
                errors.append(f"{difference_label}: source does not exist: {source_name}")
        details = difference.get("details")
        if (not isinstance(details, list) or not details or
                any(not isinstance(detail, str) or not detail for detail in details)):
            errors.append(f"{difference_label}.details: expected non-empty strings")
    return value


def validate_state_ownership(value, label, errors):
    if value is None:
        return {}
    if not isinstance(value, dict) or not value:
        errors.append(f"{label}: expected a non-empty object")
        return {}
    for owner, details in sorted(value.items()):
        require_string(owner, f"{label}.owner", errors)
        require_string(details, f"{label}.{owner}", errors)
    return value


def validate_typescript(value, label, errors):
    if value is None:
        return {}
    if not isinstance(value, dict):
        errors.append(f"{label}: expected an object")
        return {}
    require_string(value.get("interface"), f"{label}.interface", errors)
    if value.get("kind") not in TYPESCRIPT_KINDS:
        errors.append(f"{label}.kind: unsupported TypeScript generation kind")
    require_string(value.get("description"), f"{label}.description", errors)
    return value


def validate_contract(root, path, contract, errors):
    label = path.relative_to(root)
    if not isinstance(contract, dict):
        errors.append(f"{label}: contract root must be an object")
        return None
    if contract.get("version") != 1:
        errors.append(f"{label}: version must be 1")
    subsystem = contract.get("subsystem")
    require_string(subsystem, f"{label}.subsystem", errors)

    header_name = contract.get("wrapper_header")
    header_names = contract.get("wrapper_headers")
    header_source = ""
    if header_name is not None and header_names is not None:
        errors.append(f"{label}: declare wrapper_header or wrapper_headers, not both")
    if header_names is None:
        header_names = [header_name]
    elif (not isinstance(header_names, list) or not header_names or
          len(header_names) != len(set(header_names))):
        errors.append(f"{label}.wrapper_headers: expected unique header paths")
        header_names = []
    for index, candidate in enumerate(header_names):
        header_label = (f"{label}.wrapper_header" if header_name is not None
                        else f"{label}.wrapper_headers[{index}]")
        if not require_string(candidate, header_label, errors):
            continue
        header_path = root / candidate
        if not header_path.is_file():
            errors.append(f"{label}: wrapper header does not exist: {candidate}")
        else:
            header_source += header_path.read_text(encoding="utf-8") + "\n"

    service_header = contract.get("service_header")
    service_header_source = ""
    if service_header is not None and require_string(
            service_header, f"{label}.service_header", errors):
        service_header_path = root / service_header
        if not service_header_path.is_file():
            errors.append(f"{label}: service header does not exist: {service_header}")
        else:
            service_header_source = service_header_path.read_text(encoding="utf-8")

    runtimes = contract.get("runtimes")
    runtime_sources = {}
    runtime_export_sources = {}
    runtime_constant_sources = {}
    runtime_namespaces = {}
    runtime_platforms = {}
    if not isinstance(runtimes, dict) or not runtimes:
        errors.append(f"{label}.runtimes: expected a non-empty object")
        runtimes = {}
    for runtime in sorted(runtimes):
        runtime_contract = runtimes[runtime]
        runtime_label = f"{label}.runtimes.{runtime}"
        if runtime not in RUNTIMES:
            errors.append(f"{runtime_label}: unknown runtime")
        if not isinstance(runtime_contract, dict):
            errors.append(f"{runtime_label}: expected an object")
            continue
        source_name = runtime_contract.get("source")
        registration = runtime_contract.get("registration_symbol")
        runtime_namespaces[runtime] = validate_namespaces(
            runtime_contract.get("namespace"), f"{runtime_label}.namespace", errors)
        runtime_platforms[runtime] = validate_platforms(
            runtime_contract.get("platforms"), f"{runtime_label}.platforms", errors)
        extractor = runtime_contract.get("export_extractor")
        extractors = extractor if isinstance(extractor, list) else [extractor]
        if extractor is not None:
            if not extractors or any(item not in EXPORT_EXTRACTORS for item in extractors):
                errors.append(f"{runtime_label}.export_extractor: unknown extractor")
            elif len(extractors) != len(set(extractors)):
                errors.append(f"{runtime_label}.export_extractor: extractors must be unique")
        constant_export_scope = runtime_contract.get("constant_export_scope")
        if (constant_export_scope is not None and
                not any(item in CONSTANT_EXPORT_EXTRACTORS for item in extractors)):
            errors.append(
                f"{runtime_label}.constant_export_scope: "
                "requires a constant export extractor"
            )
        export_prefixes = runtime_contract.get("export_prefixes")
        if export_prefixes is not None:
            if (not isinstance(export_prefixes, list) or not export_prefixes or
                    any(not isinstance(prefix, str) or not prefix
                        for prefix in export_prefixes)):
                errors.append(f"{runtime_label}.export_prefixes: expected non-empty strings")
            elif len(export_prefixes) != len(set(export_prefixes)):
                errors.append(f"{runtime_label}.export_prefixes: prefixes must be unique")
        export_scopes = runtime_contract.get("export_scopes")
        if export_scopes is not None:
            if (not isinstance(export_scopes, list) or not export_scopes or
                    any(not isinstance(scope, str) or not scope
                        for scope in export_scopes)):
                errors.append(f"{runtime_label}.export_scopes: expected non-empty strings")
            elif len(export_scopes) != len(set(export_scopes)):
                errors.append(f"{runtime_label}.export_scopes: scopes must be unique")
        if require_string(source_name, f"{runtime_label}.source", errors):
            source_path = root / source_name
            if not source_path.is_file():
                errors.append(f"{runtime_label}: source does not exist: {source_name}")
            else:
                runtime_sources[runtime] = source_path.read_text(encoding="utf-8")
        export_source_names = runtime_contract.get("export_sources")
        if export_source_names is not None:
            if (not isinstance(export_source_names, list) or
                    not export_source_names or
                    len(export_source_names) != len(set(export_source_names)) or
                    any(not isinstance(item, str) or not item
                        for item in export_source_names)):
                errors.append(
                    f"{runtime_label}.export_sources: expected unique source paths")
            else:
                for index, export_source_name in enumerate(export_source_names):
                    export_source_path = root / export_source_name
                    if not export_source_path.is_file():
                        errors.append(
                            f"{runtime_label}.export_sources[{index}]: "
                            f"source does not exist: {export_source_name}")
                    else:
                        runtime_sources[runtime] = (
                            runtime_sources.get(runtime, "") + "\n" +
                            export_source_path.read_text(encoding="utf-8"))
        if require_string(registration, f"{runtime_label}.registration_symbol", errors):
            source = runtime_sources.get(runtime, "")
            if source and not symbol_present(source, registration):
                errors.append(f"{runtime_label}: registration symbol not found: {registration}")
        if isinstance(export_scopes, list) and export_scopes:
            source = runtime_sources.get(runtime, "")
            scoped_sources = []
            for index, scope in enumerate(export_scopes):
                if not isinstance(scope, str) or not scope:
                    continue
                scoped_source = extract_c_initializer_body(source, scope)
                if source and scoped_source is None:
                    errors.append(
                        f"{runtime_label}.export_scopes[{index}]: "
                        f"initializer not found: {scope}"
                    )
                elif scoped_source is not None:
                    scoped_sources.append(scoped_source)
            runtime_export_sources[runtime] = "\n".join(scoped_sources)
        if constant_export_scope is not None and require_string(
                constant_export_scope,
                f"{runtime_label}.constant_export_scope",
                errors):
            source = runtime_sources.get(runtime, "")
            scoped_source = extract_c_function_body(source, constant_export_scope)
            if source and scoped_source is None:
                errors.append(
                    f"{runtime_label}.constant_export_scope: "
                    f"function body not found: {constant_export_scope}"
                )
            elif scoped_source is not None:
                runtime_constant_sources[runtime] = scoped_source

    validate_error_convention(
        contract.get("error_convention"), runtimes, f"{label}.error_convention", errors)

    availability_gates = validate_availability_gates(
        root,
        contract.get("availability_gates"),
        runtimes,
        f"{label}.availability_gates",
        errors,
    )

    unsupported_runtimes = contract.get("unsupported_runtimes", {})
    if not isinstance(unsupported_runtimes, dict):
        errors.append(f"{label}.unsupported_runtimes: expected an object")
        unsupported_runtimes = {}
    for runtime in sorted(unsupported_runtimes):
        runtime_label = f"{label}.unsupported_runtimes.{runtime}"
        details = unsupported_runtimes[runtime]
        if runtime not in RUNTIMES:
            errors.append(f"{runtime_label}: unknown runtime")
        if runtime in runtimes:
            errors.append(f"{runtime_label}: runtime is also declared as supported")
        if not isinstance(details, dict):
            errors.append(f"{runtime_label}: expected an object")
            continue
        validate_platforms(details.get("platforms"), f"{runtime_label}.platforms", errors)
        require_string(details.get("reason"), f"{runtime_label}.reason", errors)
        source_name = details.get("source")
        source = ""
        if require_string(source_name, f"{runtime_label}.source", errors):
            source_path = root / source_name
            if not source_path.is_file():
                errors.append(f"{runtime_label}: source does not exist: {source_name}")
            else:
                source = source_path.read_text(encoding="utf-8")
        registration = details.get("registration_symbol")
        if registration is not None and require_string(
                registration, f"{runtime_label}.registration_symbol", errors):
            if source and not symbol_present(source, registration):
                errors.append(
                    f"{runtime_label}: registration symbol not found: {registration}"
                )
        absent_prefixes = details.get("absent_export_prefixes")
        if absent_prefixes is not None:
            if not isinstance(absent_prefixes, list) or not absent_prefixes:
                errors.append(
                    f"{runtime_label}.absent_export_prefixes: expected a non-empty array"
                )
            else:
                object_keys = extract_javascript_object_keys(source) if source else []
                for index, prefix in enumerate(absent_prefixes):
                    prefix_label = f"{runtime_label}.absent_export_prefixes[{index}]"
                    if not require_string(prefix, prefix_label, errors):
                        continue
                    matches = sorted(key for key in object_keys if key.startswith(prefix))
                    if matches:
                        errors.append(
                            f"{runtime_label}: unsupported runtime exports found for "
                            f"prefix {prefix}: {', '.join(matches)}"
                        )

    platform_differences = validate_platform_differences(
        root,
        contract.get("platform_differences"),
        f"{label}.platform_differences",
        errors,
    )
    state_ownership = validate_state_ownership(
        contract.get("state_ownership"),
        f"{label}.state_ownership",
        errors,
    )
    typescript = validate_typescript(
        contract.get("typescript"),
        f"{label}.typescript",
        errors,
    )

    capability_gate = contract.get("capability_gate")
    if capability_gate is not None:
        validate_capability_reference(
            capability_gate, f"{label}.capability_gate", errors)

    capability_relationships = contract.get("capability_relationships", [])
    if not isinstance(capability_relationships, list):
        errors.append(f"{label}.capability_relationships: expected an array")
        capability_relationships = []
    for index, relationship in enumerate(capability_relationships):
        relationship_label = f"{label}.capability_relationships[{index}]"
        validate_capability_reference(relationship, relationship_label, errors)
        if not isinstance(relationship, dict):
            continue
        applies_to = relationship.get("applies_to")
        if (not isinstance(applies_to, list) or not applies_to or
                any(not isinstance(name, str) or not name for name in applies_to)):
            errors.append(f"{relationship_label}.applies_to: expected operation names")
        elif len(applies_to) != len(set(applies_to)):
            errors.append(f"{relationship_label}.applies_to: operation names must be unique")
        relationship_runtimes = relationship.get("runtimes")
        if relationship_runtimes is not None:
            if (not isinstance(relationship_runtimes, list) or
                    not relationship_runtimes):
                errors.append(f"{relationship_label}.runtimes: expected runtime names")
            elif len(relationship_runtimes) != len(set(relationship_runtimes)):
                errors.append(f"{relationship_label}.runtimes: runtime names must be unique")
            else:
                for runtime in relationship_runtimes:
                    if runtime not in RUNTIMES:
                        errors.append(f"{relationship_label}.runtimes: unknown runtime: {runtime}")
                    elif runtime not in runtimes:
                        errors.append(f"{relationship_label}.runtimes: runtime is not declared: {runtime}")

    operations = contract.get("operations")
    if not isinstance(operations, list) or not operations:
        errors.append(f"{label}.operations: expected a non-empty array")
        operations = []
    operation_names = set()
    export_specs = {runtime: [] for runtime in runtimes}
    export_keys = set()
    for index, operation in enumerate(operations):
        operation_label = f"{label}.operations[{index}]"
        if not isinstance(operation, dict):
            errors.append(f"{operation_label}: expected an object")
            continue
        name = operation.get("name")
        wrapper_symbol = operation.get("wrapper_symbol")
        service_symbol = operation.get("service_symbol")
        name_is_valid = require_string(name, f"{operation_label}.name", errors)
        require_string(operation.get("signature"), f"{operation_label}.signature", errors)
        typescript_description = operation.get("typescript_description")
        if typescript_description is not None:
            require_string(typescript_description,
                           f"{operation_label}.typescript_description", errors)
        if name_is_valid and name in operation_names:
            errors.append(f"{operation_label}: duplicate operation name: {name}")
        if name_is_valid:
            operation_names.add(name)
        binding_symbols = operation.get("binding_symbols")
        if wrapper_symbol is None and binding_symbols is None:
            errors.append(f"{operation_label}: expected wrapper_symbol or binding_symbols")
        elif wrapper_symbol is not None and require_string(
                wrapper_symbol, f"{operation_label}.wrapper_symbol", errors):
            if header_source and not symbol_present(header_source, wrapper_symbol):
                errors.append(f"{operation_label}: wrapper symbol not declared: {wrapper_symbol}")
        if service_symbol is not None:
            if not require_string(service_symbol,
                                  f"{operation_label}.service_symbol", errors):
                pass
            elif not service_header_source:
                errors.append(f"{operation_label}: service_symbol requires service_header")
            elif not symbol_present(service_header_source, service_symbol):
                errors.append(f"{operation_label}: service symbol not declared: {service_symbol}")
        exports = operation.get("exports")
        if not isinstance(exports, dict) or not exports:
            errors.append(f"{operation_label}.exports: expected a non-empty object")
            continue
        for runtime, export_name in exports.items():
            if runtime not in runtimes:
                errors.append(f"{operation_label}.exports.{runtime}: runtime is not declared")
                continue
            specs = normalize_export_specs(
                export_name,
                f"{operation_label}.exports.{runtime}",
                runtime_namespaces.get(runtime, []),
                runtime_platforms.get(runtime, []),
                errors,
            )
            source = runtime_sources.get(runtime, "")
            for spec in specs:
                export_specs[runtime].append((name, spec))
                export_key = (runtime, spec["namespace"], spec["name"])
                if export_key in export_keys:
                    errors.append(
                        f"{operation_label}.exports.{runtime}: duplicate public export: "
                        f"{spec['namespace']}.{spec['name']}"
                    )
                export_keys.add(export_key)
                if source and spec["source_token"] not in source:
                    errors.append(
                        f"{operation_label}.exports.{runtime}: "
                        f"export token not found: {spec['source_token']}"
                    )
        if binding_symbols is not None:
            if not isinstance(binding_symbols, dict) or not binding_symbols:
                errors.append(f"{operation_label}.binding_symbols: expected a non-empty object")
            else:
                for runtime in sorted(binding_symbols):
                    binding_label = f"{operation_label}.binding_symbols.{runtime}"
                    symbol = binding_symbols[runtime]
                    if runtime not in runtimes:
                        errors.append(f"{binding_label}: runtime is not declared")
                        continue
                    if runtime not in exports:
                        errors.append(f"{binding_label}: runtime has no operation export")
                    if require_string(symbol, binding_label, errors):
                        source = runtime_sources.get(runtime, "")
                        if source and not symbol_present(source, symbol):
                            errors.append(f"{binding_label}: binding symbol not found: {symbol}")

    for index, relationship in enumerate(capability_relationships):
        if not isinstance(relationship, dict):
            continue
        for operation_name in relationship.get("applies_to", []):
            if operation_name not in operation_names:
                errors.append(
                    f"{label}.capability_relationships[{index}].applies_to: "
                    f"unknown operation: {operation_name}"
                )

    constants = contract.get("constants", [])
    if not isinstance(constants, list):
        errors.append(f"{label}.constants: expected an array")
        constants = []
    constant_names = set()
    constant_export_specs = {runtime: [] for runtime in runtimes}
    for index, constant in enumerate(constants):
        constant_label = f"{label}.constants[{index}]"
        if not isinstance(constant, dict):
            errors.append(f"{constant_label}: expected an object")
            continue
        name = constant.get("name")
        name_is_valid = require_string(name, f"{constant_label}.name", errors)
        if name_is_valid and name in constant_names:
            errors.append(f"{constant_label}: duplicate constant name: {name}")
        if name_is_valid:
            constant_names.add(name)
        if "value" not in constant or constant.get("value") is None or isinstance(
                constant.get("value"), (dict, list)):
            errors.append(f"{constant_label}.value: expected a scalar value")
        exports = constant.get("exports")
        if not isinstance(exports, dict) or not exports:
            errors.append(f"{constant_label}.exports: expected a non-empty object")
            continue
        for runtime, export_name in exports.items():
            if runtime not in runtimes:
                errors.append(f"{constant_label}.exports.{runtime}: runtime is not declared")
                continue
            specs = normalize_export_specs(
                export_name,
                f"{constant_label}.exports.{runtime}",
                runtime_namespaces.get(runtime, []),
                runtime_platforms.get(runtime, []),
                errors,
            )
            source = runtime_sources.get(runtime, "")
            for spec in specs:
                constant_export_specs[runtime].append((name, spec))
                export_key = (runtime, spec["namespace"], spec["name"])
                if export_key in export_keys:
                    errors.append(
                        f"{constant_label}.exports.{runtime}: duplicate public export: "
                        f"{spec['namespace']}.{spec['name']}"
                    )
                export_keys.add(export_key)
                if source and spec["source_token"] not in source:
                    errors.append(
                        f"{constant_label}.exports.{runtime}: "
                        f"export token not found: {spec['source_token']}"
                    )

    for runtime in sorted(runtimes):
        runtime_contract = runtimes[runtime]
        if not isinstance(runtime_contract, dict):
            continue
        extractor = runtime_contract.get("export_extractor")
        extractors = extractor if isinstance(extractor, list) else [extractor]
        source = runtime_sources.get(runtime, "")
        if extractors and all(item in EXPORT_EXTRACTORS for item in extractors) and source:
            declared = Counter(spec["name"] for _, spec in export_specs[runtime])
            if any(item in CONSTANT_EXPORT_EXTRACTORS for item in extractors):
                declared.update(
                    spec["name"] for _, spec in constant_export_specs[runtime]
                )
            constant_export_scope = runtime_contract.get("constant_export_scope")
            discovered = Counter()
            for item in extractors:
                extraction_source = runtime_export_sources.get(runtime, source)
                if item in CONSTANT_EXPORT_EXTRACTORS and constant_export_scope is not None:
                    extraction_source = runtime_constant_sources.get(runtime, "")
                discovered.update(extract_exports(extraction_source, item))
            export_prefixes = runtime_contract.get("export_prefixes")
            if isinstance(export_prefixes, list) and export_prefixes:
                discovered = Counter({
                    name: count
                    for name, count in discovered.items()
                    if any(name.startswith(prefix) for prefix in export_prefixes)
                })
            missing = discovered - declared
            extra = declared - discovered
            if missing:
                names = ", ".join(sorted(missing.elements()))
                errors.append(f"{label}.runtimes.{runtime}: uncontracted exports: {names}")
            if extra:
                names = ", ".join(sorted(extra.elements()))
                errors.append(f"{label}.runtimes.{runtime}: exports not found by extractor: {names}")

    return {
        "errorConvention": contract.get("error_convention"),
        "capabilityGate": capability_gate,
        "capabilityRelationships": capability_relationships,
        "availabilityGates": availability_gates,
        "stateOwnership": state_ownership,
        "typescript": typescript,
        "platformDifferences": platform_differences,
        "operationCount": len(operations),
        "constantCount": len(constants),
        "runtimes": {
            runtime: {
                "namespace": value.get("namespace"),
                "platforms": value.get("platforms", []),
                "exportCount": (len(export_specs.get(runtime, [])) +
                                len(constant_export_specs.get(runtime, []))),
                "constantExportCount": len(constant_export_specs.get(runtime, [])),
                "omittedOperations": [
                    operation.get("name")
                    for operation in operations
                    if runtime not in operation.get("exports", {})
                ],
            }
            for runtime, value in sorted(runtimes.items())
            if isinstance(value, dict)
        },
        "unsupportedRuntimes": {
            runtime: {
                "platforms": value.get("platforms", []),
                "reason": value.get("reason"),
                "verifiedAbsentExportPrefixes": value.get("absent_export_prefixes", []),
            }
            for runtime, value in sorted(unsupported_runtimes.items())
            if isinstance(value, dict)
        },
    }


def validate_capability_gates(root, contracts, errors):
    operations_by_subsystem = {
        contract.get("subsystem"): {
            operation.get("name")
            for operation in contract.get("operations", [])
            if isinstance(operation, dict)
        }
        for _, contract in contracts
        if isinstance(contract, dict) and isinstance(contract.get("subsystem"), str)
    }
    for path, contract in contracts:
        label = path.relative_to(root)
        references = []
        gate = contract.get("capability_gate")
        if isinstance(gate, dict):
            references.append((f"{label}.capability_gate", gate))
        relationships = contract.get("capability_relationships", [])
        if isinstance(relationships, list):
            references.extend(
                (f"{label}.capability_relationships[{index}]", relationship)
                for index, relationship in enumerate(relationships)
                if isinstance(relationship, dict)
            )
        for reference_label, reference in references:
            target_subsystem = reference.get("subsystem")
            target_operation = reference.get("operation")
            if target_subsystem not in operations_by_subsystem:
                errors.append(
                    f"{reference_label}: unknown subsystem: {target_subsystem}"
                )
            elif target_operation not in operations_by_subsystem[target_subsystem]:
                errors.append(
                    f"{reference_label}: unknown operation: "
                    f"{target_subsystem}.{target_operation}"
                )


def validate_wrapper_ownership(root, contracts, errors):
    owners = {}
    for path, contract in contracts:
        if not isinstance(contract, dict):
            continue
        subsystem = contract.get("subsystem")
        for index, operation in enumerate(contract.get("operations", [])):
            if not isinstance(operation, dict):
                continue
            symbol = operation.get("wrapper_symbol")
            if not isinstance(symbol, str) or not symbol:
                continue
            owner = f"{subsystem}.{operation.get('name')}"
            previous = owners.get(symbol)
            if previous is not None:
                label = f"{path.relative_to(root)}.operations[{index}].wrapper_symbol"
                errors.append(
                    f"{label}: wrapper symbol already owned by {previous}: {symbol}"
                )
            else:
                owners[symbol] = owner


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.repo.resolve()
    contract_dir = root / "api" / "contracts"
    errors = []
    matrix = {"version": 1, "subsystems": {}}
    contracts = []

    contract_paths = sorted(contract_dir.glob("*.json"))
    if not contract_paths:
        errors.append(f"no contracts found under {contract_dir}")
    for path in contract_paths:
        contract = load_json(path, errors)
        if contract is None:
            continue
        contracts.append((path, contract))
        summary = validate_contract(root, path, contract, errors)
        subsystem = contract.get("subsystem")
        if summary is not None and isinstance(subsystem, str):
            if subsystem in matrix["subsystems"]:
                errors.append(f"duplicate subsystem contract: {subsystem}")
            matrix["subsystems"][subsystem] = summary

    validate_capability_gates(root, contracts, errors)
    validate_wrapper_ownership(root, contracts, errors)

    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    output = args.output or root / "build" / "generated" / "api-support-matrix.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(matrix, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"Validated {len(contract_paths)} API contracts; wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())