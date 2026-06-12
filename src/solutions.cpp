#include "Transformers.h"
#include "pages.h"
#include "core.h"
#include <iostream>
#include <iomanip>

namespace core {

bool has_known_solution(size_t page_index) {
    // Lista de páginas que já possuem lógica de deciframento conhecida
    switch (page_index) {
        case 1: case 3: case 4: case 5: 
        case 6: case 7: case 8: case 9:
        case 10: case 11: case 12: case 13: case 14: case 15: case 16: 
        case 73: // Totients
        case 74: // Parable
            return true;
        default:
            return false;
    }
}

std::string get_solution_method(size_t page_index) {
    if (!has_known_solution(page_index)) return "None";
    
    if (page_index == 1) return "Atbash";
    if (page_index == 3 || page_index == 4) return "Vigenere (DIVINITY)";
    if (page_index == 5 || page_index == 10 || page_index == 11 || page_index == 12 || 
        page_index == 13 || page_index == 16 || page_index == 74) return "Plain Text / Direct";
    if (page_index >= 6 && page_index <= 9) return "Atbash (Shift 3)";
    if (page_index == 14 || page_index == 15) return "Vigenere (FIRFVMFERENFE)";
    if (page_index == 73) return "Totient Prime Function";
    
    return "Known (Other)";
}

std::vector<size_t> get_possible_interrupters(size_t page_index) {
    // Mapeamento baseado no registro PAGE_INTERUPTERS fornecido
    if (page_index == 1) return {96};
    if (page_index == 3 || page_index == 4) return {6, 15, 48, 49, 75, 85, 133, 145, 153, 160, 161, 166, 220, 251, 318, 332, 399, 422, 424, 444, 466, 471, 500, 506, 515};
    if (page_index == 5) return {37, 97, 98, 119, 144};
    if (page_index >= 6 && page_index <= 9) return {31, 47, 55, 97, 105, 118, 133, 150, 216, 221, 260, 293, 338, 343, 386, 452, 479, 485, 542, 604, 651, 660, 699, 714, 770};
    if (page_index >= 10 && page_index <= 13) return {8, 25, 70, 123, 334, 431, 471, 555};
    if (page_index == 14 || page_index == 15) return {11, 50, 59, 125, 190};
    if (page_index == 16) return {51, 52};
    if (page_index >= 17 && page_index <= 19) return {8, 18, 59, 62, 66, 92, 116, 141, 178, 184, 190, 199, 246, 253, 293, 300, 303, 346, 360, 399, 420, 438, 462, 487, 501, 509, 511, 578, 593, 619, 633, 638, 645, 719};
    if (page_index >= 20 && page_index <= 23) return {29, 61, 86, 94, 163, 186, 193, 230, 259, 282, 304, 330, 398, 472, 487, 510, 549, 555, 557, 627, 642, 679, 794};
    if (page_index == 24) return {54, 86, 150};
    if (page_index >= 25 && page_index <= 31) return {35, 162, 184, 190, 193, 197, 204, 246, 279, 294, 318, 319, 342, 389, 393, 442, 460, 481, 510, 564, 595, 660, 680, 684, 687, 764, 803, 808, 828, 853, 873, 882, 886, 889, 906, 957, 987, 990, 1004, 1023, 1099, 1116, 1122, 1138, 1157, 1163, 1175, 1204, 1207, 1294, 1298, 1310, 1323, 1351, 1357, 1371, 1381, 1411, 1414, 1446, 1459, 1464, 1508, 1568, 1595, 1691};
    if (page_index == 32) return {1};
    if (page_index >= 33 && page_index <= 39) return {10, 19, 58, 78, 88, 141, 206, 233, 252, 259, 313, 364, 369, 391, 393, 398, 460, 469, 474, 476, 516, 605, 639, 646, 648, 666, 682, 690, 738, 776, 796, 802, 805, 878, 888, 904, 921, 929, 955, 958, 1015, 1069, 1105, 1107, 1120, 1156, 1184, 1186, 1210, 1213, 1255, 1261, 1263, 1314, 1338, 1398, 1400, 1532, 1575, 1584, 1596, 1651, 1714, 1750, 1754, 1777, 1815, 1819, 1883, 1888, 1892};
    if (page_index == 73) return {56};
    if (page_index == 74) return {34, 60, 68};
    return {};
}

bool apply_known_solution(const Page& page, ProcessedText& pt) {
    size_t idx = page.index;

    // 1. Página 1: Atbash
    if (idx == 1) {
        AtbashTransformer().transform(pt);
        return true;
    }

    // 2. Traduções Diretas
    if (idx == 5 || idx == 10 || idx == 11 || idx == 12 || idx == 13 || idx == 16 || idx == 74) {
        return true;
    }

    // 3. Páginas 3 e 4: Vigenere (DIVINITY) - Fluxo Contínuo
    if (idx == 3 || idx == 4) {
        // Página 3 (WELCOME... END TO SELF)
        VigenereTransformer vt("ᛞᛁᚢᛁᚾᛁᛏᚣ", {48, 74, 84, 132, 159, 160, 250, 421, 443, 465, 514});
        vt.transform(pt);
        return true;
    }

    // 4. Página 73: Totient Prime Function
    if (idx == 73) {
        TotientPrimeTransformer tp(false, {56}, 1, false);
        tp.transform(pt);
        return true;
    }

    // 5. Páginas 14 e 15: Vigenere (FIRFVMFERENFE)
    if (idx == 14 || idx == 15) {
        VigenereTransformer vt("ᚠᛁᚱᚠᚢᛗᚠᛖᚱᛖᚾᚠᛖ", {49, 58});
        vt.transform(pt);
        return true;
    }

    // 6. Páginas 6, 7, 8 e 9: Atbash com shift 3
    if (idx >= 6 && idx <= 9) {
        AtbashTransformer(3).transform(pt);
        return true;
    }

    return false;
}

} // namespace core
