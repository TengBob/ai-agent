import QtQuick
import QtQuick.Shapes

Item {
    property int   used: 0
    property int   total: 4096
    property bool  compact: false

    width:  compact ? 80 : 100
    height: compact ? 24 : 60

    // compact mode: just text
    Text {
        visible: compact
        anchors.centerIn: parent
        text: {
            const k = Math.floor(used / 1000)
            return k > 0 ? k + "k tokens" : used + " tokens"
        }
        font.pixelSize: 11
        color: {
            const r = used / Math.max(total, 1)
            if (r > 0.8) return "#F44336"
            if (r > 0.5) return "#FF9800"
            return "#4CAF50"
        }
    }

    // full mode: arc + text
    Shape {
        visible: !compact
        anchors.centerIn: parent
        width: 52; height: 52

        ShapePath {
            strokeWidth: 5
            strokeColor: "#E0E0E0"
            fillColor: "transparent"
            PathAngleArc {
                centerX: 26; centerY: 26; radiusX: 22; radiusY: 22
                startAngle: -220; sweepAngle: 260
            }
        }
        ShapePath {
            strokeWidth: 5
            strokeColor: {
                const r = used / Math.max(total, 1)
                if (r > 0.8) return "#F44336"
                if (r > 0.5) return "#FF9800"
                return "#4CAF50"
            }
            fillColor: "transparent"
            PathAngleArc {
                centerX: 26; centerY: 26; radiusX: 22; radiusY: 22
                startAngle: -220
                sweepAngle: Math.min(1, used / Math.max(total, 1)) * 260
            }
        }
    }

    Column {
        visible: !compact
        anchors.centerIn: parent
        spacing: 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Math.floor(used / 1000) > 0 ? Math.floor(used/1000) + "k" : String(used)
            font.pixelSize: 12; font.bold: true; color: "#424242"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "tokens"; font.pixelSize: 9; color: "#9E9E9E"
        }
    }
}
