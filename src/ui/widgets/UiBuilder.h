#pragma once

#include <QDialog>
#include <QMenuBar>
#include <QStringList>

#include <functional>
#include <memory>

class QAction;

namespace CardStack {

class UiBuilder {
public:
    struct DialogContext {
        QString deckName;
        QString deckDescription;
        QStringList fieldNames;
        QStringList templateNames;
        QStringList reportNames;
        QStringList recentSearches;
        QStringList recentReplacements;
    };

    static bool populateMenuBar(
        QMenuBar* menuBar,
        int menuId,
        QObject* actionParent,
        const std::function<void(QAction*)>& configureAction);

    static std::unique_ptr<QDialog> createDialog(
        const QString& dialogName,
        QWidget* parent = nullptr,
        const DialogContext& context = {});
    static QWidget* controlById(QWidget* parent, int controlId);
    static void setColorDialogState(QDialog* dialog, const QStringList& colors, bool useSystemColors);
    static QStringList colorDialogColors(const QDialog* dialog);
    static bool colorDialogUsesSystemColors(const QDialog* dialog);
    static QStringList dialogNames();
};

} // namespace CardStack
