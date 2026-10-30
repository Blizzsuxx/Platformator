# Contract: Compiled Behavior Catalog

## Registration compatibility

Keep `registerScriptFactory(typeName, factory)` and existing `SERIALIZABLE_SCRIPT(Type, fields...)` source calls working. Add optional lazy descriptor registration and registry enumeration in editor-independent engine types. A macro-generated static provider has class-member access and uses unevaluated member types; registration/catalog enumeration never constructs a Behavior, resolves references, initializes SDL/Runtime, or loads assets.

Metadata is evaluated only on explicit export. Normal frame/update paths do not traverse descriptors; registration adds only provider/name state. Within a compatible target, factory-only registrations remain runnable and report schema unavailable. For the supported shared-runner baseline below, recompiling with new headers is sufficient; no game-source rewrite or external reflection generator is required. Existing static-registration/link retention semantics remain authoritative; only names actually registered by that executable appear as available.

## Runner command

`GAME_EXECUTABLE --editor-catalog` returns one UTF-8 JSON document on stdout and status 0, before ordinary runtime option parsing/Runtime construction. Errors go to stderr/nonzero. Reject incompatible scene/run options in catalog mode rather than running the game accidentally. Normal runtime arguments keep existing meaning. This additive command belongs to `platformator::run(argc, argv, defaultScene)`.

The no-game-source-rewrite compatibility baseline covers stock `main`, `mario_example`, and other targets using that shared entry with existing serialization macros. A rebuild supplies the command. A custom entry bypassing the shared runner is not automatically catalog-compatible: it remains editable/buildable, but its owner must explicitly integrate the export protocol before automatic Run is supported. The editor never rewrites that game's source.

Project settings contain `catalogProtocol`: `common-runner-v1` or `unavailable`. Known bundled targets default to `common-runner-v1`; other targets default to `unavailable` until explicitly declared compatible. This declaration is permission to attempt the query, not evidence that behaviors are registered. Do not probe or automatically launch an undeclared custom entry. Keep document editing and Build-only available, display the unsupported protocol state, and explain the integration requirement. Only a successful current query certifies availability.

Example envelope (representative schema, not a generated implementation):

```json
{
  "schema_version": 1,
  "behaviors": [
    {
      "name": "MarioCameraRig",
      "field_schema_available": true,
      "fields": [
        {"name": "camera", "kind": "object_reference", "target": "Camera", "nullable": true},
        {"name": "target", "kind": "object_reference", "target": "GameObject", "nullable": true}
      ]
    }
  ]
}
```

Sort behavior names deterministically; field order follows macro serialization order. Empty registry emits an empty array. Reject duplicate inconsistent descriptors, unsupported envelope versions, invalid shapes, oversized (4 MiB) output, nonzero status, extra non-JSON stdout, or a query exceeding 10 seconds. Keep diagnostics separately; never parse them as partial schema.

## Field descriptors

Kinds: boolean, signed/unsigned integer (with representable range), number, string, enum (known values where available), vector2, rectangle, array (recursive element descriptor), optional/nullability, asset_reference (texture/audio/animation), object_reference (expected engine object/component kind), and opaque_json. Supported standard containers recurse; unknown custom serializable types are opaque unless safe explicit metadata is supplied.

Object references serialize as an integer file ID or null, not a nested reference object. Asset references serialize as their established string/null form. Preserve those contracts and validate target-kind/root restrictions in field editors.

Descriptors identify types, not C++ constructor values. Export must never instantiate a default object to discover values. Adding a macro behavior stores only its type initially; missing declared fields display uncommitted UI drafts and can be explicitly materialized by the developer. Existing `WITH_DEFAULT` runtime deserialization retains constructor/member defaults for omitted keys. Factory-only custom deserializers may require manual data; do not promise this omission policy for arbitrary custom implementations.

Existing JSON samples can provide observed shapes for opaque fields, labeled as samples rather than authoritative schemas/defaults. Recursive JSON editing preserves type and unrecognized content. Fields absent from a new catalog are not automatically deleted. Scene-only behavior names remain preserved/unregistered; a registered factory without field metadata remains registered/schema-unavailable, a distinct state.

## Editor cache and launch checks

The editor wraps catalog data with canonical project path, selected profile/target/executable, binary identity, relevant source/CMake input identity, schema version and query outcome. Persist raw catalog metadata within preferences, never a game executable/source bundle.

`Unavailable -> Current` requires successful query of the successfully built selected executable. Target/profile/source/binary changes or failed build/query cause `Current -> Stale`; last known descriptors may assist editing with a visible stale label but cannot certify current run availability. An executable without the command yields Unavailable and actionable rebuild/compatibility instructions.

For a declared compatible target, Build & Run always configures/builds, exports catalog, validates all scene behavior type names against that result, then launches only on success. An undeclared target stops before executable query/launch and remains available for Build-only. Snapshot project/scene/root/options for this operation; source changes detected during the pipeline invalidate the verification and require a new user run rather than an automatic retry. Standalone game runtime errors remain Console failures, never a reason to alter scene data silently.

## Acceptance

Compile/execute descriptor tests for private members, zero/one/multiple fields, supported engine/container/reference types, and factory-only registration. Count constructors to prove query performs none; verify no Runtime and no SDL initialization. Check deterministic output, missing-field defaults at actual game load, unsupported custom types, stale/target-mismatched catalogs, bad/oversized/timeout output, and explicit `--scene/--asset-root` normal launch. Use a shared-runner fixture plus Mario to prove no game-source rewrite or editor-linked game scripts are required. An undeclared custom-main fixture must remain editable/buildable and retain identical source while query/Run are blocked; a falsely declared fixture must fail verification without launching gameplay.
