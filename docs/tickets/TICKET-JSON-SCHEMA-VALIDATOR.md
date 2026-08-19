# TICKET-JSON-SCHEMA-VALIDATOR — JSON Schema validator for chronon.render-plan v1

## Stato: DONE (chore atomic su `main`)

## Problema

`src/c_api/chronon3d_c_api.cpp::compile_plan()` (introdotto dall'espansione
WIP del main) eseguiva solo un check manuale di due campi:

```cpp
if (root.value("schema", std::string{}) != "chronon.render-plan")
    throw std::runtime_error("unsupported plan schema");
if (root.value("version", 0) != 1)
    throw std::runtime_error("unsupported render plan version");
```

Questo è in contraddizione con il principio fail-loud del progetto:
campi mancanti, campi sconosciuti, type mismatch e vincoli `minimum`/
`minimumLength`/`enum` venivano **silenziosamente ignorati** o producevano
errori a valle confusi ("unsupported layer type: " nel caso di type enum
mismatch).

Lo schema canonico `schemas/chronon.render-plan.v1.schema.json` (Draft
2020-12 con `additionalProperties: false`, required/const/enum/integer+minimum,
nested objects per canvas/layers[].animation/output) NON
veniva MAI consultato dal codice di compilazione — solo il commento
implicito "fidati di quello che ti arriva".

Audit corollario (vedi AUDIT diagnostic del main): "Lo schema JSON promette
funzionalità che l'implementazione ignora" — P0 fix.

## Soluzione

Validatore table-driven hand-rolled (no nuova dipendenza esterna da
`nlohmann/json-schema-validator`):

- Header: `include/chronon3d/render_plan/render_plan_validator.hpp`
  (PUBLIC, namespace `chronon3d::render_plan`)
- Impl: `src/render_plan/render_plan_validator.cpp` (~280 LoC)
- CMake: `src/render_plan/CMakeLists.txt` → OBJECT lib
  `chronon3d_render_plan` linkato a `nlohmann::json` + `chronon3d_core_impl`
- Wire-in: `src/c_api/chronon3d_c_api.cpp` (2 call sites)

### API pubblica

```cpp
namespace chronon3d::render_plan {

enum class ValidationIssueKind { MissingField, UnknownField, WrongType,
                                  ConstMismatch, EnumMismatch, BelowMinimum,
                                  AboveMaximum, StringTooShort, ArrayTooShort,
                                  ArrayTooLong, UnsupportedKeyword };

struct ValidationIssue {
    std::string path;        // JSON-pointer-like, es. "layers[2].type"
    ValidationIssueKind kind;
    std::string expected;    // cosa richiedeva lo schema
    std::string actual;      // cosa era presente
    std::string detail;
    std::string to_string() const;
};

struct ValidationResult {
    std::vector<ValidationIssue> issues;
    bool ok() const noexcept;
    std::string format() const;
};

ValidationResult validate_render_plan(const nlohmann::json& root);
void validate_render_plan_or_throw(const nlohmann::json& root);

}  // namespace chronon3d::render_plan
```

### Subset JSON Schema supportato

| Keyword                  | Note                                                              |
| ------------------------ | ----------------------------------------------------------------- |
| `type`                   | object, array, integer, number, string, boolean, null            |
| `const`                  | strict equality                                                   |
| `enum`                   | set membership                                                    |
| `required`               | array di nomi di proprietà (object-level)                         |
| `additionalProperties`   | true/false (sealed objects)                                       |
| `minLength`              | string only                                                       |
| `minimum`                | numeric only                                                      |
| `exclusiveMinimum`       | numeric only (strict <)                                           |
| `maximum`                | numeric only                                                      |
| `minItems`               | array only                                                        |
| `maxItems`               | array only                                                        |
| `properties`             | nested subschema walk                                             |
| `items`                  | nested array element subschema walk                               |

### Comportamento fail-loud

`validate_render_plan_or_throw(root)` accumula **tutti** i problemi in una
singola passata (no fail-fast), poi solleva `std::runtime_error` con la
lista formattata. La C API esistente mappa `std::exception` →
`CHRONON_ERROR_PARSE_FAILED` senza modifiche.

### Wire-in C API

| Sito                                              | Prima                            | Dopo                                       |
| ------------------------------------------------- | -------------------------------- | ------------------------------------------ |
| `compile_plan(const json&)`                       | manuale `schema`+`version` check | `validate_render_plan_or_throw(root)`      |
| `render_legacy_json()` dopo `legacy_scene_to_plan`| (nessuna validazione)            | `validate_render_plan_or_throw(root)`      |

### Tests

`tests/c_abi/test_render_plan_validator.cpp` con 4 TEST_CASE + 5 SUBCASE
diagnostici (chiusura dei vincoli del contratto):

1. Plan valido minimo → `ok()`
2. Plan con campo extra (root-level `totally_extra_field`) → `UnknownField`
3. Plan senza `output.path` (interpretazione "campo required mancante",
   vedi "Interpretazione spec test" sotto) → `MissingField`
4. Plan con `canvas.width = "abc"` (type mismatch) → `WrongType`
5. Nested required violation (`layers[0].id` mancante) → path corretto
6. Multi-issue accumulation (5 violazioni simultanee) → tutte presenti
7. `const` mismatch su `schema` → `ConstMismatch` (non `WrongType`)
8. `minLength` violation su `output.path` → `StringTooShort`
9. Root non-object (array) → `WrongType` senza crash

### Interpretazione spec test

Il prompt originale chiedeva "plan senza codec respinto" — ma `codec` è
OPZIONALE nello schema (`output.required = ["path"]`, non `codec`).
L'interpretazione scelta è "campo required mancante" con `output.path`
come campo target. L'interpretazione è documentata nel commento del
SUBCASE 3 del test file.

## File toccati (chore atomic)

- NEW: `include/chronon3d/render_plan/render_plan_validator.hpp`
- NEW: `src/render_plan/render_plan_validator.cpp`
- NEW: `src/render_plan/CMakeLists.txt`
- NEW: `tests/c_abi/test_render_plan_validator.cpp`
- NEW: `tests/c_abi_tests.cmake`
- NEW: `docs/tickets/TICKET-JSON-SCHEMA-VALIDATOR.md` (questo file)
- MOD: `src/CMakeLists.txt` (1 add_subdirectory)
- MOD: `src/c_api/chronon3d_c_api.cpp` (3 patch surgical: include +
  2 wire-in)
- MOD: `tests/manifests/test_definitions.cmake` (1 entry)

Totale: 6 NEW + 3 MOD = 9 file. Subject envelope ≤ 72 char.

## Forward-points

1. **WBH macchina-verify**: il dev host corrente ha `CHRONON3D_BUILD_CLI=OFF`
   per default e `CHRONON3D_BUILD_C_API=OFF` (vedi
   `build/lfd/CMakeCache.txt:69`). Il build del test
   `chronon3d_c_abi_tests` richiede reconfigure con `BUILD_TESTS=ON` (default)
   ma senza dipendenze da CLI/C_API. Eseguire quando si ha un working
   build host con doctest accessibile.
2. **Schema evolution**: se la prossima milestone aggiunge un campo al
   schema, ricordarsi di aggiornare ANCHE l'inlined schema literal in
   `render_plan_validator.cpp::validate_render_plan` (Cat-3 anti-dup).
   Aggiungere un futuro `tools/check_schema_validator_alignment.sh` per
   verificare automaticamente.
3. **JSON Schema spec subset**: se lo schema dovesse usare `$ref`,
   `oneOf`, `allOf`, `anyOf`, `pattern`, o `format`, l'attuale validatore
   emette `UnsupportedKeyword` come fail-loud. Estendere il subset se
   necessario.
4. **Batch validation**: considerare un helper `validate_many(plans)`
   che produce un sommario aggregato per casi d'uso di CI bulk-check
   (TICKET-FUTURE-CI-BATCH-VALIDATION).
