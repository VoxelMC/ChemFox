#include "core/commands/Commands.h"

namespace cd {

SnapshotCommand::SnapshotCommand(DocumentHost* host, const QString& text, Document before, Document after,
                                 int mergeKey)
    : QUndoCommand(text), host_(host), before_(std::move(before)), after_(std::move(after)), mergeKey_(mergeKey) {}

void SnapshotCommand::undo() {
    host_->document() = before_;
    host_->documentReplaced();
}

void SnapshotCommand::redo() {
    // Also runs on push, which applies the edit for the first time.
    host_->document() = after_;
    host_->documentReplaced();
}

bool SnapshotCommand::mergeWith(const QUndoCommand* other) {
    auto* o = dynamic_cast<const SnapshotCommand*>(other);
    if (!o || o->mergeKey_ != mergeKey_ || mergeKey_ < 0) return false;
    after_ = o->after_;
    return true;
}

}  // namespace cd
