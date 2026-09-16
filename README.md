# SIC/XE Assembler (C++) — Blocks, Literals, Expressions, **EQU/ORG**, Multi‑M

This rebuild includes everything plus **EQU**, **ORG**, and **full expression relocation** for `WORD` and format‑4 fields.

## What’s supported
- **Directives**: `START, END, BYTE, WORD, RESB, RESW, BASE, NOBASE, LTORG, USE, EQU, ORG`
- **Opcodes**: a broad set (formats 3/4) and format‑2 (`CLEAR, COMPR, ADDR, TIXR`)
- **Addressing**: simple / `#` immediate / `@` indirect / `,X` indexed
- **Literals**: `=C'..'`, `=X'..'`, `=W'<expr>'` with `LTORG`
- **Program blocks**: `USE <name>`
- **Expressions**: absolute arithmetic with `+ - * /` and parentheses
- **Relocation**: multiple `M` records for each symbol term in a `WORD` or format‑4 address field

### Relocation rule (SIC/XE)
If `(count(+symbols) − count(−symbols)) == 1`, the expression is **relocatable** and we emit `M` records for each term (+/−). Otherwise, it’s treated as **absolute**.

> Note: For education clarity, `M` records include a trailing `+` or `-` sign in this assembler’s object file. Real SIC/XE includes the sign only when external references are present; loaders infer `+` to the control section. You can adapt the emitter easily.

## Build
```bash
g++ -std=c++17 -O2 -o sicxe SIC_XE_Assembler.cpp
```

## Run
```bash
./sicxe sample_input.asm listing.lst object.obj
```

## The Sample Program (sample_input.asm / expr_reloc.asm) has been assembled 
## Corresponding Listing File (listing.list) and Object Program File (object.obj) is also created
