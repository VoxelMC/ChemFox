# Licences for code compiled into RDKit

conda-forge's `librdkit` package ships only RDKit's own licence, but its libraries also contain these
projects. `collect-licenses.ps1` copies the files here into the app's `licenses/rdkit-bundled/` folder.

| File | Project | Where it was taken from |
|---|---|---|
| `InChI-1.06-LICENSE.txt` | InChI 1.06 (in `RDKitInchi`) | github.com/IUPAC-InChI/InChI, tag v1.06 |
| `coordgenlibs-LICENSE.txt` | coordgenlibs (in `RDKitcoordgen`) | github.com/schrodinger/coordgenlibs |
| `maeparser-LICENSE.txt` | maeparser (in `RDKitmaeparser`) | github.com/schrodinger/maeparser |
| `RingDecomposerLib-LICENSE.txt` | RingDecomposerLib (in `RDKitRingDecomposerLib`) | github.com/rareylab/RingDecomposerLib |

Check these when RDKit is upgraded: the InChI version is printed inside `RDKitInchi.dll`
("Software v. 1.06").
