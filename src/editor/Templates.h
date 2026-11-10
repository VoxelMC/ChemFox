#pragma once
// Built-in structure templates, inserted from the Structure menu or the canvas right-click menu.

#include <QString>
#include <vector>

namespace cd::templates {

struct Template {
    QString name;
    QString smiles;
};

struct Group {
    QString name;
    std::vector<Template> items;
};

const std::vector<Group>& all();

}  // namespace cd::templates
