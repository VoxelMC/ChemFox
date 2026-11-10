#pragma once
// Every edit goes through a command so undo/redo covers everything.
// A command stores the document before and after the edit; documents are small,
// so snapshots stay cheap and every edit is undoable by construction.

#include "core/model/Document.h"

#include <QUndoCommand>
#include <functional>

namespace cd {

class DocumentHost {
public:
    virtual ~DocumentHost() = default;
    virtual Document& document() = 0;
    virtual void documentReplaced() = 0;  // called after undo/redo swaps the document
};

class SnapshotCommand : public QUndoCommand {
public:
    // mergeKey > 0 merges consecutive commands with the same key (e.g. nudges).
    SnapshotCommand(DocumentHost* host, const QString& text, Document before, Document after, int mergeKey = -1);
    void undo() override;
    void redo() override;
    int id() const override { return mergeKey_; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    DocumentHost* host_;
    Document before_, after_;
    int mergeKey_;
};

}  // namespace cd
