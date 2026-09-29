import QtQuick
import org.qindaqt.kwin

ScreenEdgeHandler {
    edge: ScreenEdgeHandler.LeftEdge
    mode: ScreenEdgeHandler.Touch
    onActivated: {
        Workspace.slotToggleShowDesktop();
    }
}
