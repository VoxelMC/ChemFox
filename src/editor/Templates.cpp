#include "editor/Templates.h"

#include "core/model/Labels.h"

namespace cd::templates {

const std::vector<Group>& all() {
    static const std::vector<Group> groups = [] {
        std::vector<Group> g;
        g.push_back({"Rings",
                     {{"Benzene", "c1ccccc1"},
                      {"Naphthalene", "c1ccc2ccccc2c1"},
                      {"Pyridine", "c1ccncc1"},
                      {"Pyrrole", "c1cc[nH]c1"},
                      {"Furan", "c1ccoc1"},
                      {"Thiophene", "c1ccsc1"},
                      {"Indole", "c1ccc2[nH]ccc2c1"},
                      {"Imidazole", "c1c[nH]cn1"},
                      {"Pyrimidine", "c1cncnc1"},
                      {"Purine", "c1ncc2[nH]cnc2n1"},
                      {"Cyclopropane", "C1CC1"},
                      {"Cyclopentane", "C1CCCC1"},
                      {"Cyclohexane", "C1CCCCC1"},
                      {"Piperidine", "C1CCNCC1"},
                      {"Morpholine", "C1COCCN1"},
                      {"Tetrahydrofuran", "C1CCOC1"}}});
        g.push_back({"Amino acids (L)",
                     {{"Alanine (Ala, A)", "N[C@@H](C)C(=O)O"},
                      {"Arginine (Arg, R)", "N[C@@H](CCCNC(=N)N)C(=O)O"},
                      {"Asparagine (Asn, N)", "N[C@@H](CC(N)=O)C(=O)O"},
                      {"Aspartate (Asp, D)", "N[C@@H](CC(=O)O)C(=O)O"},
                      {"Cysteine (Cys, C)", "N[C@@H](CS)C(=O)O"},
                      {"Glutamine (Gln, Q)", "N[C@@H](CCC(N)=O)C(=O)O"},
                      {"Glutamate (Glu, E)", "N[C@@H](CCC(=O)O)C(=O)O"},
                      {"Glycine (Gly, G)", "NCC(=O)O"},
                      {"Histidine (His, H)", "N[C@@H](Cc1c[nH]cn1)C(=O)O"},
                      {"Isoleucine (Ile, I)", "CC[C@H](C)[C@H](N)C(=O)O"},
                      {"Leucine (Leu, L)", "N[C@@H](CC(C)C)C(=O)O"},
                      {"Lysine (Lys, K)", "N[C@@H](CCCCN)C(=O)O"},
                      {"Methionine (Met, M)", "N[C@@H](CCSC)C(=O)O"},
                      {"Phenylalanine (Phe, F)", "N[C@@H](Cc1ccccc1)C(=O)O"},
                      {"Proline (Pro, P)", "OC(=O)[C@@H]1CCCN1"},
                      {"Serine (Ser, S)", "N[C@@H](CO)C(=O)O"},
                      {"Threonine (Thr, T)", "C[C@@H](O)[C@H](N)C(=O)O"},
                      {"Tryptophan (Trp, W)", "N[C@@H](Cc1c[nH]c2ccccc12)C(=O)O"},
                      {"Tyrosine (Tyr, Y)", "N[C@@H](Cc1ccc(O)cc1)C(=O)O"},
                      {"Valine (Val, V)", "N[C@@H](C(C)C)C(=O)O"}}});
        g.push_back({"Sugars",
                     {{"β-D-Glucose (pyranose)", "OC[C@H]1O[C@@H](O)[C@H](O)[C@@H](O)[C@@H]1O"},
                      {"D-Glucose (open chain)", "OC[C@@H](O)[C@@H](O)[C@H](O)[C@@H](O)C=O"},
                      {"β-D-Ribose (furanose)", "OC[C@H]1O[C@@H](O)[C@H](O)[C@@H]1O"},
                      {"2-Deoxy-β-D-ribose", "OC[C@H]1O[C@@H](O)C[C@@H]1O"},
                      {"β-D-Fructose (furanose)", "OC[C@H]1O[C@@](O)(CO)[C@@H](O)[C@@H]1O"}}});
        g.push_back({"Cofactors",
                     {{"ATP", "Nc1ncnc2c1ncn2[C@@H]1O[C@H](COP(=O)(O)OP(=O)(O)OP(=O)(O)O)[C@@H](O)[C@H]1O"},
                      {"Pyridoxal phosphate (PLP)", "Cc1ncc(COP(=O)(O)O)c(C=O)c1O"},
                      {"Thiamine pyrophosphate (TPP)", "Cc1ncc(C[n+]2csc(CCOP(=O)(O)OP(=O)(O)O)c2C)c(N)n1"},
                      {"S-Adenosylmethionine (SAM)",
                       "C[S+](CC[C@H](N)C(=O)O)C[C@H]1O[C@@H](n2cnc3c(N)ncnc32)[C@H](O)[C@@H]1O"},
                      // Coenzyme A: the CoA abbreviation's fragment plus its thiol sulfur.
                      {"Coenzyme A (CoA-SH)", "S" + labels::fragmentSmiles("CoA").mid(1)},
                      {"Acetyl-CoA (abbreviated)", "CC(=O)S* |$;;;;CoA$|"}}});
        g.push_back({"Compounds",
                     {{"Aspirin", "CC(=O)Oc1ccccc1C(=O)O"},
                      {"Caffeine", "Cn1cnc2c1c(=O)n(C)c(=O)n2C"},
                      {"Ibuprofen", "CC(C)Cc1ccc(cc1)[C@@H](C)C(=O)O"},
                      {"Acetic acid", "CC(=O)O"},
                      {"Ethyl acetate", "CCOC(C)=O"},
                      {"Triphenylphosphine", "c1ccc(cc1)P(c1ccccc1)c1ccccc1"}}});
        return g;
    }();
    return groups;
}

}  // namespace cd::templates
