#include "io/Native.h"

#include "core/chem/Chem.h"
#include "core/model/Labels.h"
#include "io/Export.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <functional>

namespace cd::io {

namespace {

QString stereoName(BondStereo s) {
    switch (s) {
    case BondStereo::Wedge: return "wedge";
    case BondStereo::Hash: return "hash";
    case BondStereo::Wavy: return "wavy";
    default: return "none";
    }
}
BondStereo stereoFrom(const QString& s) {
    if (s == "wedge") return BondStereo::Wedge;
    if (s == "hash") return BondStereo::Hash;
    if (s == "wavy") return BondStereo::Wavy;
    return BondStereo::None;
}
QString dbposName(DbPos p) {
    switch (p) {
    case DbPos::Left: return "left";
    case DbPos::Right: return "right";
    case DbPos::Center: return "center";
    default: return "auto";
    }
}
DbPos dbposFrom(const QString& s) {
    if (s == "left") return DbPos::Left;
    if (s == "right") return DbPos::Right;
    if (s == "center") return DbPos::Center;
    return DbPos::Auto;
}
QJsonArray strings(const QStringList& l) { return QJsonArray::fromStringList(l); }
QStringList strings(const QJsonValue& v) {
    QStringList out;
    for (const auto& x : v.toArray()) out << x.toString();
    return out;
}
double round4(double x) { return std::round(x * 10000.0) / 10000.0; }

// Migrations: index i upgrades version i+1 to i+2. Version 1 is current, so none yet.
const std::vector<std::function<void(QJsonObject&)>>& migrations() {
    static const std::vector<std::function<void(QJsonObject&)>> m = {};
    return m;
}

}  // namespace

bool migrate(QJsonObject& o, QString* error) {
    int v = o.value("version").toInt(1);
    if (v > kFormatVersion) {
        if (error) *error = QString("This file was written by a newer version (format %1).").arg(v);
        return false;
    }
    while (v < kFormatVersion) {
        migrations()[v - 1](o);
        o["version"] = ++v;
    }
    return true;
}

QJsonObject toJsonObject(const Document& d, bool withSmiles) {
    QJsonArray objects;
    for (const auto& m : d.molecules) {
        QJsonObject mo{{"id", m.id}, {"type", "molecule"}};
        if (!m.number.isEmpty()) mo["number"] = m.number;
        if (m.numberManual) mo["numberManual"] = true;
        if (!m.caption.isEmpty()) mo["caption"] = m.caption;
        if (m.showStereo) mo["showStereo"] = true;
        if (m.showCarbons) mo["showCarbons"] = true;
        if (!m.aromatic.isEmpty()) mo["aromatic"] = m.aromatic;
        if (withSmiles) mo["smiles"] = chem::smiles(m);
        QJsonArray atoms, bonds;
        for (const auto& a : m.atoms) {
            QJsonObject ao{{"id", a.id}, {"el", a.el}, {"x", round4(a.x)}, {"y", round4(a.y)}, {"charge", a.charge}};
            ao["label"] = a.label.isEmpty() ? QJsonValue() : QJsonValue(a.label);
            if (a.radical) ao["radical"] = a.radical;
            if (a.map) ao["map"] = a.map;
            if (a.isotope) ao["isotope"] = a.isotope;
            atoms.append(ao);
        }
        for (const auto& b : m.bonds) {
            QJsonObject bo{{"id", b.id}, {"a", b.a}, {"b", b.b}, {"order", b.order}, {"stereo", stereoName(b.stereo)}};
            if (b.dbpos != DbPos::Auto) bo["dbpos"] = dbposName(b.dbpos);
            bonds.append(bo);
        }
        mo["atoms"] = atoms;
        mo["bonds"] = bonds;
        if (!m.groups.empty()) {
            QJsonArray groups;
            for (const auto& g : m.groups)
                groups.append(QJsonObject{{"label", g.label}, {"anchor", g.anchor}, {"atoms", strings(g.atoms)}});
            mo["groups"] = groups;
        }
        objects.append(mo);
    }
    for (const auto& a : d.arrows) {
        QJsonObject ao{{"id", a.id}, {"type", "arrow"}, {"kind", arrowKindName(a.kind)},
                       {"x1", round4(a.x1)}, {"y1", round4(a.y1)}, {"x2", round4(a.x2)},
                       {"y2", round4(a.y2)}, {"above", a.above}, {"below", a.below}};
        if (!a.via.empty()) {
            QJsonArray via;
            for (const auto& p : a.via) via.append(QJsonArray{round4(p.x()), round4(p.y())});
            ao["via"] = via;
        }
        if (!a.color.isEmpty()) ao["color"] = a.color;
        if (!a.headStyle.isEmpty()) ao["head"] = a.headStyle;
        if (a.bothHeads) ao["bothHeads"] = true;
        if (!a.line.isEmpty()) ao["line"] = a.line;
        if (a.widthScale != 1.0) ao["widthScale"] = a.widthScale;
        objects.append(ao);
    }
    for (const auto& t : d.texts) {
        QJsonObject to{{"id", t.id}, {"type", "text"}, {"x", round4(t.x)}, {"y", round4(t.y)}, {"text", t.text}};
        if (t.size > 0) to["size"] = t.size;
        if (!t.color.isEmpty()) to["color"] = t.color;
        if (!t.align.isEmpty()) to["align"] = t.align;
        objects.append(to);
    }
    for (const auto& s : d.schemes) {
        QJsonArray steps;
        for (const auto& st : s.steps) {
            QJsonObject so{{"reactants", strings(st.reactants)}, {"arrow", st.arrow}, {"products", strings(st.products)}};
            if (!st.agents.isEmpty()) so["agents"] = strings(st.agents);
            if (st.showDiff) so["diff"] = true;
            if (st.manualMap) so["manualMap"] = true;
            steps.append(so);
        }
        objects.append(QJsonObject{{"id", s.id}, {"type", "scheme"}, {"steps", steps}});
    }

    QJsonObject root{{"format", "chemfox"}, {"version", kFormatVersion}, {"style", d.styleName}, {"objects", objects}};
    if (!d.styleText.isEmpty()) root["styleText"] = d.styleText;
    if (d.stepLetters) root["stepLetters"] = true;
    if (d.showMaps) root["showMaps"] = true;
    QStringList codes(d.suppressedCodes.begin(), d.suppressedCodes.end());
    QStringList inst(d.suppressedInstances.begin(), d.suppressedInstances.end());
    codes.sort();
    inst.sort();
    root["warnings"] = QJsonObject{{"suppressed", strings(codes)}, {"suppressedInstances", strings(inst)}};
    // Custom groups used here travel with the file, so it means the same on another computer.
    QJsonObject groups;
    const auto custom = labels::customGroups(), fromFiles = labels::documentGroups();
    for (const auto& m : d.molecules)
        for (const auto& a : m.atoms) {
            if (a.label.isEmpty()) continue;
            QString name = labels::canonical(a.label);
            if (custom.contains(name) || fromFiles.contains(name)) groups[name] = labels::fragmentSmiles(name);
        }
    if (!groups.isEmpty()) root["groups"] = groups;
    return root;
}

QByteArray toJson(const Document& d, bool withSmiles) {
    return QJsonDocument(toJsonObject(d, withSmiles)).toJson(QJsonDocument::Indented);
}

std::optional<Document> fromJsonObject(QJsonObject o, QString* error) {
    const QString format = o.value("format").toString();
    if (format != "chemfox" && format != "chemdoc") {  // "chemdoc": the working name before ChemFox
        if (error) *error = "Not a ChemFox document.";
        return std::nullopt;
    }
    if (!migrate(o, error)) return std::nullopt;
    // Groups defined in the file fill in names this computer doesn't know (the user's own win).
    QMap<QString, QString> groups;
    const QJsonObject go = o.value("groups").toObject();
    for (auto it = go.begin(); it != go.end(); ++it)
        if (labels::checkGroup(it.key(), it.value().toString()).isEmpty()) groups[it.key()] = it.value().toString();
    labels::addFileGroups(groups);
    Document d;
    d.styleName = o.value("style").toString("default");
    d.styleText = o.value("styleText").toString();
    d.stepLetters = o.value("stepLetters").toBool();
    d.showMaps = o.value("showMaps").toBool();
    QJsonObject w = o.value("warnings").toObject();
    for (const auto& c : strings(w.value("suppressed"))) d.suppressedCodes.insert(c);
    for (const auto& c : strings(w.value("suppressedInstances"))) d.suppressedInstances.insert(c);

    for (const auto& v : o.value("objects").toArray()) {
        QJsonObject ob = v.toObject();
        QString type = ob.value("type").toString();
        if (type == "molecule") {
            Molecule m;
            m.id = ob.value("id").toString();
            m.number = ob.value("number").toString();
            m.numberManual = ob.value("numberManual").toBool();
            m.caption = ob.value("caption").toString();
            m.showStereo = ob.value("showStereo").toBool();
            m.showCarbons = ob.value("showCarbons").toBool();
            m.aromatic = ob.value("aromatic").toString();
            for (const auto& av : ob.value("atoms").toArray()) {
                QJsonObject ao = av.toObject();
                Atom a;
                a.id = ao.value("id").toString();
                a.el = ao.value("el").toString("C");
                a.x = ao.value("x").toDouble();
                a.y = ao.value("y").toDouble();
                a.charge = ao.value("charge").toInt();
                a.radical = ao.value("radical").toInt();
                a.label = ao.value("label").toString();
                a.map = ao.value("map").toInt();
                a.isotope = ao.value("isotope").toInt();
                m.atoms.push_back(a);
            }
            for (const auto& bv : ob.value("bonds").toArray()) {
                QJsonObject bo = bv.toObject();
                Bond b;
                b.id = bo.value("id").toString();
                b.a = bo.value("a").toString();
                b.b = bo.value("b").toString();
                b.order = std::clamp(bo.value("order").toInt(1), 1, 3);
                b.stereo = stereoFrom(bo.value("stereo").toString());
                b.dbpos = dbposFrom(bo.value("dbpos").toString());
                if (m.atom(b.a) && m.atom(b.b)) m.bonds.push_back(b);
            }
            for (const auto& gv : ob.value("groups").toArray()) {
                QJsonObject go = gv.toObject();
                m.groups.push_back({go.value("label").toString(), go.value("anchor").toString(), strings(go.value("atoms"))});
            }
            m.pruneGroups();
            // A molecule saved with only SMILES (written by another tool) gets a layout.
            if (m.atoms.empty() && ob.contains("smiles")) {
                if (auto fresh = chem::fromSmiles(ob.value("smiles").toString())) {
                    QString id = m.id;
                    m.atoms = fresh->atoms;
                    m.bonds = fresh->bonds;
                    for (auto& a : m.atoms) a.id = id + "_" + a.id;
                    for (auto& b : m.bonds) { b.id = id + "_" + b.id; b.a = id + "_" + b.a; b.b = id + "_" + b.b; }
                    // Optional orientation for hand-written files: "rotate" (degrees), "flip" (mirror left-right).
                    double rot = ob.value("rotate").toDouble() * M_PI / 180.0;
                    bool flip = ob.value("flip").toBool();
                    QPointF c0 = m.center();
                    for (auto& a : m.atoms) {
                        QPointF p = a.pos() - c0;
                        if (flip) p.setX(-p.x());
                        a.setPos(QPointF(p.x() * std::cos(rot) - p.y() * std::sin(rot),
                                         p.x() * std::sin(rot) + p.y() * std::cos(rot)));
                    }
                    if (flip)  // mirroring swaps wedge and hash
                        for (auto& b : m.bonds)
                            b.stereo = b.stereo == BondStereo::Wedge ? BondStereo::Hash
                                     : b.stereo == BondStereo::Hash ? BondStereo::Wedge : b.stereo;
                    if (ob.contains("x") || ob.contains("y")) {
                        // Placed by hand: centre the structure there.
                        m.translate(QPointF(ob.value("x").toDouble(), ob.value("y").toDouble()) - m.center());
                    } else {
                        // Side by side for now; the editor arranges schemes on open.
                        double x = 0;
                        for (const auto& prev : d.molecules) x = std::max(x, prev.bounds().right() + 2.0);
                        m.translate(QPointF(x - m.bounds().left(), 0));
                        d.needsLayout = true;
                    }
                }
            }
            d.molecules.push_back(m);
        } else if (type == "arrow") {
            Arrow a;
            a.id = ob.value("id").toString();
            a.kind = arrowKindFromName(ob.value("kind").toString());
            a.x1 = ob.value("x1").toDouble();
            a.y1 = ob.value("y1").toDouble();
            a.x2 = ob.value("x2").toDouble(a.x1 + 3);
            a.y2 = ob.value("y2").toDouble(a.y1);
            a.above = ob.value("above").toString();
            a.below = ob.value("below").toString();
            a.color = ob.value("color").toString();
            a.headStyle = ob.value("head").toString();
            a.bothHeads = ob.value("bothHeads").toBool();
            a.line = ob.value("line").toString();
            a.widthScale = ob.value("widthScale").toDouble(1.0);
            for (const auto& v : ob.value("via").toArray()) {
                QJsonArray p = v.toArray();
                a.via.emplace_back(p.at(0).toDouble(), p.at(1).toDouble());
            }
            d.arrows.push_back(a);
        } else if (type == "text") {
            TextBox t;
            t.id = ob.value("id").toString();
            t.x = ob.value("x").toDouble();
            t.y = ob.value("y").toDouble();
            t.text = ob.value("text").toString();
            t.size = ob.value("size").toDouble();
            t.color = ob.value("color").toString();
            t.align = ob.value("align").toString();
            d.texts.push_back(t);
        } else if (type == "scheme") {
            Scheme s;
            s.id = ob.value("id").toString();
            for (const auto& sv : ob.value("steps").toArray()) {
                QJsonObject so = sv.toObject();
                Step st;
                st.reactants = strings(so.value("reactants"));
                st.agents = strings(so.value("agents"));
                st.arrow = so.value("arrow").toString();
                st.products = strings(so.value("products"));
                st.showDiff = so.value("diff").toBool();
                st.manualMap = so.value("manualMap").toBool();
                s.steps.push_back(st);
            }
            d.schemes.push_back(s);
        }
    }
    d.pruneSchemes();
    d.fixNextId();
    return d;
}

std::optional<Document> fromJson(const QByteArray& bytes, QString* error) {
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = "Invalid JSON: " + pe.errorString();
        return std::nullopt;
    }
    return fromJsonObject(doc.object(), error);
}

bool saveFile(const QString& path, const Document& d, QString* error) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(toJson(d));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

std::optional<Document> loadFile(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return std::nullopt;
    }
    QByteArray bytes = f.readAll();
    if (path.endsWith(".svg", Qt::CaseInsensitive)) return documentFromSvg(bytes, error);
    return fromJson(bytes, error);
}

}  // namespace cd::io
