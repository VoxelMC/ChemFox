#pragma once
// Native JSON format ("chemfox", versioned with migrations; files from before the rename say "chemdoc").

#include "core/model/Document.h"

#include <QByteArray>
#include <QJsonObject>
#include <optional>

namespace cd::io {

// Version 2 added mechanism arrows and lone pairs. A document that uses neither is still written
// as version 1, so older ChemFox versions keep opening it.
constexpr int kFormatVersion = 2;

// `withSmiles` regenerates each molecule's SMILES so other tools can read the chemistry.
QJsonObject toJsonObject(const Document& d, bool withSmiles = true);
QByteArray toJson(const Document& d, bool withSmiles = true);
std::optional<Document> fromJson(const QByteArray& bytes, QString* error = nullptr);
std::optional<Document> fromJsonObject(QJsonObject o, QString* error = nullptr);

// Bring an older file up to kFormatVersion. Returns false if it is from the future.
bool migrate(QJsonObject& o, QString* error = nullptr);

bool saveFile(const QString& path, const Document& d, QString* error = nullptr);
std::optional<Document> loadFile(const QString& path, QString* error = nullptr);

}  // namespace cd::io
