#pragma once

class QDir;
class QString;
class QWidget;

namespace CardStack::Tests {

void installVisualTestFonts();
bool saveWidgetImage(QWidget& widget, const QDir& outputDirectory, const QString& fileName);

} // namespace CardStack::Tests
