# TypeScript fragments

`scripts/generate-types.sh` extracts most of `types/budo.d.ts` from the
`JS_CFUNC_DEF` registrations in the JS binding sources. A few sections of the
public surface live entirely outside the registration tables and cannot be
recovered by the extractor:

* Functions registered via post-init `JS_Eval` shims (e.g. the staged
  neural verbs `setInput` / `getOutput` — see `js_neural_bindings.c`).
* Whole sub-namespaces that are exposed as records of values rather than
  function tables (e.g. `sys.capabilities` — discovered structurally from
  `js_capabilities_bindings.c`, but the per-entry doc strings live here).
* Interfaces that describe arguments / return shapes rather than functions
  (`GLDrawOptions`, `UdpMessage`, `CapabilityEntry`, `TensorInfo`, …).
* The handwritten 3D pipeline section of `SysGL`, whose signatures rely on
  literal-typed string unions and tuple types that the C source's `argc`
  field cannot express.

Each fragment in this directory is spliced into the generated output at a
fixed point by `generate-types.sh`. Edit the fragment, regenerate, and the
diff against `types/budo.d.ts` should remain empty.

`generated-basics.d.ts.frag` is the exception: it is generated from the
capabilities, device, and math contracts by
`scripts/generate-contract-artifacts.py`. Edit the contracts, not that file.

| Fragment file              | Splice point                                        |
|----------------------------|-----------------------------------------------------|
| `sysgl-3d.d.ts.frag`       | Inside `interface SysGL { … }` after extracted GL fns. |
| `gl-options.d.ts.frag`     | After the `SysGL` block closes (top-level interface). |
| `generated-basics.d.ts.frag` | After `GLDrawOptions`, before `SysCanvas`; generated from contracts. |
| `sysneural-staged.d.ts.frag` | Inside `interface SysNeural { … }` after extracted neural fns. |
| `sysudp.d.ts.frag`         | After `SysDevice`, before `SysCapabilities`.        |

The `Sys` interface itself is emitted by the script. When you add a new
top-level sub-namespace, list it both in the script (so the `Sys` interface
gains a `readonly <name>: Sys<Name>` line) and via a fragment that defines
the interface itself.
