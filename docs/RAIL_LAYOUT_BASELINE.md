# Bottom Icon Rail Baseline

This is the approved compact layout for the three icon rails in the Layers
surface.  The live values are owned by `BottomRailMetrics` in `MainWindow.cpp`.
Do not introduce individual numeric `setGeometry` values for these rails.

| Rail | Y | Height | Gap after |
| --- | ---: | ---: | ---: |
| Toolbar | 0 | 40 px | 1 px |
| Modes | 41 px | 48 px | 1 px |
| Panels | 90 px | 40 px | n/a |

The Layers heading starts at 140 px and the compact two-column controls begin
at 170 px (142 px high), followed by the in-card artboard rail.  The
compile-time assertion protects the 1 px geometry baseline from accidental
changes.
