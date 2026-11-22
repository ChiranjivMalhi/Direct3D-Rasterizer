#include "../../main.cpp"
#include <iostream>
int main() {
    Model actual = load_model_from_obj("Direct3D Rasterizer/st4.obj");
    std::cout << "st4.obj: " << actual.vertices.size() << " vertices, " << actual.indicies.size() << " indices\n";
    if (actual.vertices.empty() || actual.indicies.empty() || actual.indicies.size() % 3) return 1;
    for (auto index : actual.indicies) if (index >= actual.vertices.size()) return 2;
    const char* cases[] = {"f 1/1/1 2/2/1 3/3/1", "f 1//1 2//1 3//1", "f -3//-1 -2//-1 -1//-1", "f 1/1/999 2/2/1 3/3/1"};
    const char* fixture = "Direct3D Rasterizer/x64/Debug/loader-fixture.obj";
    for (int i = 0; i < 4; ++i) {
        { std::ofstream out(fixture); out << "v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\n" << cases[i] << '\n'; }
        Model model = load_model_from_obj(fixture);
        if (i < 3 ? model.vertices.size() != 3 || model.indicies.size() != 3 : !model.indicies.empty()) return 3 + i;
    }
    std::cout << "OBJ format, negative-index, and invalid-index checks passed.\n";
    return 0;
}
