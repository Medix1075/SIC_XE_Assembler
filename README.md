# SIC/XE Assembler — C++17

A two-pass **SIC/XE assembler** implemented in C++17 for assembling SIC/XE assembly programs into a **listing file** and **object program**. The implementation focuses on the core assembler pipeline while supporting program blocks, literals, expressions, `EQU`, `ORG`, and relocation-aware modification records.

## Features

### Instruction support
- Format 3 and Format 4 instructions
- Format 2 instructions including `CLEAR`, `COMPR`, `ADDR`, and `TIXR`
- Extended format using the `+` prefix, e.g. `+JSUB`, `+STA`, `+LDB`
- Addressing modes:
  - Simple/direct: `LDA VALUE`
  - Immediate: `LDA #5`
  - Indirect: `LDA @VALUE`
  - Indexed: `LDA TABLE,X`

### Assembler directives

| Directive | Purpose |
|---|---|
| `START` | Defines the starting address of the program |
| `END` | Marks the end of the source program |
| `BYTE` | Defines character, hexadecimal, or byte data |
| `WORD` | Defines a 3-byte word and supports expressions |
| `RESB` | Reserves bytes |
| `RESW` | Reserves words |
| `BASE` | Enables base-relative addressing |
| `NOBASE` | Disables base-relative addressing |
| `LTORG` | Places pending literals into the current literal pool |
| `USE` | Switches between program blocks |
| `EQU` | Defines a symbol using a constant, symbol, or expression |
| `ORG` | Temporarily changes the location counter using an absolute expression |

### Literals

The assembler supports:

```asm
=C'HELLO'
=X'F1A2'
=W'BUFFER'
```

Literal pools are emitted at `LTORG` or at the end of the program. `=W'<expression>'` literals can reference symbols and participate in relocation handling.

### Expressions and relocation

Expressions support:

```text
+  -  *  /  ( )
```

Operands can contain constants, hexadecimal constants (`0x...`), and previously defined symbols.

For relocation, the assembler tracks symbol terms in expressions. An expression is treated as **relocatable** when the net number of positive symbol terms minus negative symbol terms is `1`; otherwise it is treated as absolute. Relocatable `WORD` expressions and format-4 fields generate the corresponding `M` records.

> **Note:** This project is an educational implementation and does not implement SIC/XE control sections (`CSECT`, `EXTDEF`, `EXTREF`). Its modification-record emitter also includes an explicit `+`/`-` sign for each tracked term to make relocation behavior easier to inspect.

## How it works

The assembler follows a conventional **two-pass architecture**.

### Pass 1 — Analysis and address assignment

Pass 1 reads the source program and builds the information required for code generation:

1. Parses labels, opcodes, directives, and operands.
2. Builds the **symbol table (SYMTAB)**.
3. Maintains a separate location counter for each program block created through `USE`.
4. Processes `EQU` and `ORG`.
5. Collects literals and assigns them to literal pools at `LTORG` or program end.
6. Calculates program-block sizes and converts block-relative offsets into final addresses.

### Pass 2 — Object-code generation

Pass 2 uses the symbol/literal tables generated in Pass 1 to:

1. Resolve instruction operands and addressing modes.
2. Generate Format 2, 3, and 4 object code.
3. Select **PC-relative** addressing when the displacement fits.
4. Fall back to **base-relative** addressing when `BASE` is active and PC-relative addressing is insufficient.
5. Generate object code for `BYTE` and `WORD`.
6. Generate relocation/modification records for supported relocatable expressions.
7. Produce the final listing and object-program files.

## Object program

The generated object program uses the standard SIC/XE record structure:

- `H` — Header record
- `T` — Text records, split according to contiguity, program blocks, and the 30-byte record limit
- `M` — Modification records for supported relocatable expressions
- `E` — End record

The assembler also generates a human-readable listing containing addresses, object code, labels, operations, and operands.

## Build

### Linux / macOS / MinGW

```bash
g++ -std=c++17 -O2 -o sicxe SIC_XE_Assembler.cpp
```

On Windows with MinGW, the same command produces `sicxe.exe`.

## Usage

The executable expects **three command-line arguments**:

```text
sicxe <input.asm> <listing.lst> <object.obj>
```

Example:

```bash
./sicxe sample_input.asm sample_listing.lst sample_object.obj
```

On Windows:

```powershell
.\sicxe.exe sample_input.asm sample_listing.lst sample_object.obj
```

The first argument is the assembly source file. The second is the generated listing file, and the third is the generated object-program file.

## Repository structure

```text
SIC_XE_Assembler/
├── SIC_XE_Assembler.cpp       # Main assembler implementation
├── sample_input.asm            # Basic sample program
├── sample_listing.lst          # Example generated listing
├── sample_object.obj           # Example generated object program
├── listing.lst                 # Additional listing output
├── object.obj                  # Additional object output
├── SIC/
│   └── XE_Assembler_sample_program.asm
└── examples/
    ├── blocks_literals.asm     # Program blocks + literals example
    └── expr_reloc.asm          # Expressions + relocation example
```

## Example: program blocks and literals

The repository includes an example combining `USE`, literals, Format 4 instructions, and relocation:

```asm
TEST    START   0
        LDA     #5
        USE     CODE
LOOP    +JSUB   SUBR
        JLT     ENDL
        USE     DEFAULT
BUF     RESB    16
        USE     CODE
SUBR    CLEAR   X
        LDA     =W'BUF'
        LTORG
        RSUB
ENDL    LDA     =C'Z'
        +STA    OUT
OUT     RESW    1
        END     TEST
```

See [`examples/blocks_literals.asm`](examples/blocks_literals.asm) for the complete source.

## Example: base-relative addressing

When a target cannot be encoded using PC-relative displacement, `BASE` can be used to enable base-relative addressing:

```asm
+LDB    #TABLE2
BASE    TABLE2
LOOP    ADD     TABLE,X
        ADD     TABLE2,X
```

The included `examples/expr_reloc.asm` demonstrates this behavior together with large reserved tables and Format 4 instructions.

## Generated files

### Listing file (`.lst`)

The listing contains, for each source line:

```text
address    object-code    label    operation    operand
```

This makes it possible to inspect how source instructions and directives map to memory addresses and generated object code.

### Object file (`.obj`)

The object program contains the generated `H`, `T`, `M`, and `E` records and can be inspected to understand SIC/XE object-program generation and relocation.

## Error handling

The assembler reports errors for several invalid situations, including:

- Missing input file or invalid command-line usage
- Undefined symbols
- Duplicate symbols
- Invalid `EQU` usage
- Invalid use of `BASE`
- `ORG` expressions containing relocatable symbols
- Base-relative displacements outside the supported range
- Addresses that cannot be represented with PC-relative or base-relative addressing
- Missing literals from a literal pool

## Scope and limitations

This implementation is intended for **learning and coursework**, rather than as a complete production SIC/XE assembler.

Current limitations include:

- No control-section support (`CSECT`, `EXTDEF`, `EXTREF`)
- `ORG` is restricted to absolute expressions
- Expression relocation follows the simplified net-symbol-term rule described above
- The implementation uses a fixed opcode/register table defined in the source
- Input syntax/error validation is intentionally lightweight compared with a production assembler

## Concepts demonstrated

This project provides a practical implementation of several **Systems Programming and Compiler/Assembler concepts**:

- Two-pass assembler design
- Symbol-table construction and resolution
- Location counters
- Program-block management
- Literal pools
- Instruction encoding
- Immediate, indirect, indexed, PC-relative, and base-relative addressing
- Expression evaluation
- Relocation and modification records
- Object-program generation
- Assembly listing generation

## References

The implementation follows the core SIC/XE assembler and object-program concepts commonly presented in systems-programming coursework, particularly the two-pass assembler model, instruction formats, addressing modes, program blocks, literals, and relocation.
