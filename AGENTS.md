# guff agent instructions

## Project description
guff reads a stream of points from a file / stdin and plots them to stdout in ASCII or to SVG.

## Build & test
```bash
make          # builds guff and test_guff
make test     # runs test suite
make clean    # clean build artifacts
```

## Architecture

### SVG generation
- `svg.c`: Main SVG generation in `svg_plot()`
- `args.c`: CLI parsing, theme initialization in `init_svg()` (line 177)
- Axis labels: `-X` (x-axis) and `-Y` (y-axis) CLI flags

## File layout
- `*.c`, `*.h`: C99 source files
- `test_*.c`: Unit tests using `greatest.h` framework
- `Makefile`: Standard POSIX make
- `man/`: ronn-format manpages
- Dependencies: POSIX only, `-lm` for math

## Theming
SVG theme controlled via:
- Environment variables: `GUFF_BG_COLOR`, `GUFF_AXIS_COLOR`, etc.
- Defaults in `args.c:197-215`
- Color palettes: standard (line 153) vs colorblind-safe (line 164)
