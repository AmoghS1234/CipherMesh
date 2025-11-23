#pragma once
#include <QString>
#include <QList>
#include <QColor>

namespace CipherMesh {
namespace Themes {

// Structure defining everything a theme needs
struct ThemeDef {
    QString id;              // Internal ID (e.g., "light")
    QString name;            // Display Name
    QString styleSheet;      // QSS
    QColor actionIconColor;  // Icon color for New/Delete buttons
    QColor uiIconColor;      // Icon color for Settings/Folders
};

// --- 1. Professional Dark (Refined Dark Mode) ---
const QString Style_Professional = R"(
    /* Base */
    QMainWindow, QDialog, QMessageBox { 
        background-color: #181818; 
        color: #e4e4e4; 
        font-family: "Segoe UI", -apple-system, BlinkMacSystemFont; 
        font-size: 14px; 
    }
    
    /* Lists */
    QListWidget { 
        background-color: #1e1e1e; 
        color: #e0e0e0; 
        border: 1px solid #2a2a2a; 
        border-radius: 8px; 
        font-size: 14px; 
        outline: none; 
        padding: 6px;
    }
    QListWidget::item { 
        padding: 11px 12px; 
        border-radius: 6px;
        margin: 1px 0px;
    }
    QListWidget::item:selected { 
        background-color: #264f78; 
        color: #ffffff; 
        font-weight: 500;
    }
    QListWidget::item:hover { 
        background-color: #282828; 
    }
    
    /* Inputs */
    QLineEdit { 
        background-color: #2a2a2a; 
        color: #e4e4e4; 
        border: 1px solid #3d3d3d; 
        border-radius: 5px; 
        padding: 8px 11px;
        selection-background-color: #264f78;
    }
    QLineEdit:focus { 
        border: 2px solid #569cd6; 
        background-color: #252525;
        padding: 7px 10px;
    }
    QLineEdit:read-only { 
        background-color: #1e1e1e; 
        color: #909090; 
        border: 1px solid #2a2a2a; 
    }
    
    QTextEdit { 
        background-color: #1e1e1e; 
        color: #e4e4e4; 
        border: 1px solid #2a2a2a; 
        border-radius: 5px; 
        padding: 8px;
        selection-background-color: #264f78;
    }
    QTextEdit:focus {
        border: 2px solid #569cd6;
        padding: 7px;
    }
    
    QComboBox {
        background-color: #2a2a2a;
        color: #e4e4e4;
        border: 1px solid #3d3d3d;
        border-radius: 5px;
        padding: 7px 10px;
        padding-right: 25px;
    }
    QComboBox:hover {
        border: 1px solid #4d4d4d;
        background-color: #2e2e2e;
    }
    QComboBox:focus {
        border: 2px solid #569cd6;
        padding: 6px 9px;
        padding-right: 24px;
    }
    QComboBox::drop-down {
        border: none;
        width: 20px;
    }
    QComboBox::down-arrow {
        image: none;
        border-left: 4px solid transparent;
        border-right: 4px solid transparent;
        border-top: 5px solid #909090;
        margin-right: 5px;
    }
    QComboBox QAbstractItemView {
        background-color: #252525;
        color: #e0e0e0;
        border: 1px solid #3d3d3d;
        selection-background-color: #264f78;
        outline: none;
    }
    
    QCheckBox {
        color: #e0e0e0;
        spacing: 8px;
    }
    QCheckBox::indicator {
        width: 17px;
        height: 17px;
        border-radius: 3px;
        border: 1px solid #3d3d3d;
        background-color: #2a2a2a;
    }
    QCheckBox::indicator:checked {
        background-color: #569cd6;
        border: 1px solid #569cd6;
        image: none;
    }
    QCheckBox::indicator:hover {
        border: 1px solid #4d4d4d;
        background-color: #2e2e2e;
    }

    /* Standard Buttons */
    QPushButton { 
        background-color: #2a2a2a; 
        color: #e4e4e4; 
        border: 1px solid #3d3d3d; 
        border-radius: 5px; 
        padding: 8px 16px; 
        font-weight: 500; 
    }
    QPushButton:hover { 
        background-color: #323232; 
        border-color: #4d4d4d; 
    }
    QPushButton:pressed { 
        background-color: #222222; 
    }
    QPushButton:disabled { 
        background-color: #1e1e1e; 
        color: #606060; 
        border: 1px solid #2a2a2a; 
    }
    
    /* Action Buttons */
    QPushButton#NewButton { 
        background-color: #16a34a; 
        border: none; 
        color: #ffffff;
        font-weight: 600;
    }
    QPushButton#NewButton:hover { 
        background-color: #15803d; 
    }
    QPushButton#NewButton:pressed {
        background-color: #166534;
    }
    
    QPushButton#DeleteButton, QPushButton#RejectButton { 
        background-color: #dc2626; 
        border: none; 
        color: #ffffff;
        font-weight: 600;
    }
    QPushButton#DeleteButton:hover { 
        background-color: #b91c1c; 
    }
    QPushButton#DeleteButton:pressed {
        background-color: #991b1b;
    }
    
    QPushButton#InviteButton { 
        background-color: #f59e0b; 
        border: none; 
        color: #ffffff;
        font-weight: 600;
    }
    QPushButton#InviteButton:hover {
        background-color: #d97706;
    }
    
    QPushButton#IconButton { 
        background-color: transparent; 
        border: none; 
        padding: 8px; 
    }
    QPushButton#IconButton:hover { 
        background-color: #2a2a2a; 
        border-radius: 5px; 
    }
    
    /* Text Labels */
    QLabel { 
        color: #e4e4e4; 
    }
    QLabel#DialogTitle { 
        font-size: 19px; 
        font-weight: 600; 
        color: #ffffff; 
        margin-bottom: 8px; 
    }
    QLabel#DetailUsername { 
        font-size: 21px; 
        font-weight: 600; 
        color: #ffffff; 
    }
    QLabel#PlaceholderLabel { 
        font-size: 15px; 
        color: #808080; 
        font-style: italic; 
    }
    QLabel#StatusLabel {
        font-size: 11px;
        color: #f59e0b;
    }
    QLabel#InviteInfoLabel {
        font-size: 16px;
        padding: 20px;
    }
    
    /* Menu */
    QMenuBar {
        background-color: #1e1e1e;
        color: #e0e0e0;
        border-bottom: 1px solid #2a2a2a;
        padding: 2px 0px;
    }
    QMenuBar::item {
        padding: 6px 14px;
        background-color: transparent;
        border-radius: 4px;
    }
    QMenuBar::item:selected {
        background-color: #2a2a2a;
    }
    
    QMenu { 
        background-color: #252525; 
        border: 1px solid #3d3d3d; 
        color: #e0e0e0; 
        border-radius: 6px;
        padding: 6px;
    }
    QMenu::item { 
        padding: 8px 30px 8px 14px;
        border-radius: 4px;
    }
    QMenu::item:selected { 
        background-color: #264f78; 
        color: #ffffff; 
    }
    QMenu::separator {
        height: 1px;
        background: #2a2a2a;
        margin: 4px 8px;
    }
    
    /* Splitter */
    QSplitter::handle { 
        background-color: #2a2a2a; 
    }
    QSplitter::handle:hover {
        background-color: #3d3d3d;
    }
    
    /* Progress Bar */
    QProgressBar {
        border: 1px solid #2a2a2a;
        border-radius: 4px;
        background-color: #1e1e1e;
        text-align: center;
        color: #e4e4e4;
    }
    QProgressBar::chunk {
        border-radius: 3px;
    }
    
    /* Slider */
    QSlider::groove:horizontal {
        border: 1px solid #2a2a2a;
        height: 5px;
        background: #1e1e1e;
        border-radius: 3px;
    }
    QSlider::handle:horizontal {
        background: #569cd6;
        border: none;
        width: 15px;
        margin: -5px 0;
        border-radius: 7px;
    }
    QSlider::handle:horizontal:hover {
        background: #6aabdf;
    }
)";

// --- 2. Modern Light (Refined Clean Theme) ---
const QString Style_ModernLight = R"(
    /* Base */
    QMainWindow, QDialog, QMessageBox { 
        background-color: #fafafa; 
        color: #1a1a1a;
        font-family: "Segoe UI", -apple-system, BlinkMacSystemFont; 
        font-size: 14px; 
    }
    
    /* Lists */
    QListWidget { 
        background-color: #ffffff; 
        color: #1a1a1a; 
        border: 1px solid #d4d4d4; 
        border-radius: 8px; 
        font-size: 14px; 
        outline: none; 
        padding: 6px;
    }
    QListWidget::item { 
        padding: 11px 12px; 
        border-radius: 6px;
        margin: 1px 0px;
    }
    QListWidget::item:selected { 
        background-color: #0078d4;
        color: #ffffff; 
        font-weight: 500;
    }
    QListWidget::item:hover { 
        background-color: #f0f0f0; 
    }

    /* Inputs */
    QLineEdit { 
        background-color: #ffffff; 
        color: #1a1a1a; 
        border: 1px solid #c8c8c8; 
        border-radius: 5px; 
        padding: 8px 11px;
        selection-background-color: #cce4f7;
    }
    QLineEdit:focus { 
        border: 2px solid #0078d4; 
        background-color: #ffffff;
        padding: 7px 10px;
    }
    QLineEdit:read-only { 
        background-color: #f5f5f5; 
        color: #707070; 
        border: 1px solid #d4d4d4; 
    }
    
    QTextEdit { 
        background-color: #ffffff; 
        color: #1a1a1a; 
        border: 1px solid #c8c8c8; 
        border-radius: 5px; 
        padding: 8px;
        selection-background-color: #cce4f7;
    }
    QTextEdit:focus {
        border: 2px solid #0078d4;
        padding: 7px;
    }
    
    QComboBox {
        background-color: #ffffff;
        color: #1a1a1a;
        border: 1px solid #c8c8c8;
        border-radius: 5px;
        padding: 7px 10px;
        padding-right: 25px;
    }
    QComboBox:hover {
        border: 1px solid #a8a8a8;
        background-color: #fafafa;
    }
    QComboBox:focus {
        border: 2px solid #0078d4;
        padding: 6px 9px;
        padding-right: 24px;
    }
    QComboBox::drop-down {
        border: none;
        width: 20px;
    }
    QComboBox::down-arrow {
        image: none;
        border-left: 4px solid transparent;
        border-right: 4px solid transparent;
        border-top: 5px solid #606060;
        margin-right: 5px;
    }
    QComboBox QAbstractItemView {
        background-color: #ffffff;
        color: #1a1a1a;
        border: 1px solid #c8c8c8;
        selection-background-color: #0078d4;
        selection-color: #ffffff;
        outline: none;
    }
    
    QCheckBox {
        color: #1a1a1a;
        spacing: 8px;
    }
    QCheckBox::indicator {
        width: 17px;
        height: 17px;
        border-radius: 3px;
        border: 1px solid #c8c8c8;
        background-color: #ffffff;
    }
    QCheckBox::indicator:checked {
        background-color: #0078d4;
        border: 1px solid #0078d4;
        image: none;
    }
    QCheckBox::indicator:hover {
        border: 1px solid #a8a8a8;
        background-color: #fafafa;
    }

    /* Standard Buttons */
    QPushButton { 
        background-color: #ffffff; 
        color: #1a1a1a; 
        border: 1px solid #c8c8c8; 
        border-radius: 5px; 
        padding: 8px 16px; 
        font-weight: 500; 
    }
    QPushButton:hover { 
        background-color: #f5f5f5; 
        border-color: #a8a8a8; 
    }
    QPushButton:pressed { 
        background-color: #e8e8e8; 
    }
    QPushButton:disabled { 
        background-color: #f5f5f5; 
        color: #a0a0a0; 
        border: 1px solid #d4d4d4; 
    }

    /* Action Buttons */
    QPushButton#NewButton { 
        background-color: #0f7b38; 
        color: #ffffff; 
        border: none; 
        font-weight: 600;
    }
    QPushButton#NewButton:hover { 
        background-color: #0d6b31; 
    }
    QPushButton#NewButton:pressed {
        background-color: #0b5a29;
    }

    QPushButton#DeleteButton, QPushButton#RejectButton { 
        background-color: #d13438; 
        color: #ffffff; 
        border: none; 
        font-weight: 600;
    }
    QPushButton#DeleteButton:hover { 
        background-color: #b92b2f; 
    }
    QPushButton#DeleteButton:pressed {
        background-color: #a02428;
    }

    QPushButton#InviteButton { 
        background-color: #e67700; 
        color: #ffffff; 
        border: none; 
        font-weight: 600;
    }
    QPushButton#InviteButton:hover {
        background-color: #cf6b00;
    }

    QPushButton#IconButton { 
        background-color: transparent; 
        border: none; 
        padding: 8px;
    }
    QPushButton#IconButton:hover { 
        background-color: #f0f0f0; 
        border-radius: 5px; 
    }

    /* Text Labels */
    QLabel { 
        color: #1a1a1a; 
    }
    QLabel#DialogTitle { 
        font-size: 19px; 
        font-weight: 600; 
        color: #000000; 
        margin-bottom: 8px; 
    }
    QLabel#DetailUsername { 
        font-size: 21px; 
        font-weight: 600; 
        color: #000000; 
    }
    QLabel#PlaceholderLabel { 
        font-size: 15px; 
        color: #909090; 
        font-style: italic; 
    }
    QLabel#StatusLabel {
        font-size: 11px;
        color: #e67700;
    }
    QLabel#InviteInfoLabel {
        font-size: 16px;
        padding: 20px;
    }
    
    /* Menu */
    QMenuBar {
        background-color: #ffffff;
        color: #1a1a1a;
        border-bottom: 1px solid #d4d4d4;
        padding: 2px 0px;
    }
    QMenuBar::item {
        padding: 6px 14px;
        background-color: transparent;
        border-radius: 4px;
    }
    QMenuBar::item:selected {
        background-color: #f0f0f0;
    }
    
    QMenu { 
        background-color: #ffffff; 
        border: 1px solid #c8c8c8; 
        color: #1a1a1a; 
        border-radius: 6px;
        padding: 6px;
    }
    QMenu::item { 
        padding: 8px 30px 8px 14px;
        border-radius: 4px;
    }
    QMenu::item:selected { 
        background-color: #0078d4; 
        color: #ffffff; 
    }
    QMenu::separator {
        height: 1px;
        background: #d4d4d4;
        margin: 4px 8px;
    }
    
    /* Splitter */
    QSplitter::handle { 
        background-color: #d4d4d4; 
    }
    QSplitter::handle:hover {
        background-color: #c8c8c8;
    }
    
    /* Progress Bar */
    QProgressBar {
        border: 1px solid #d4d4d4;
        border-radius: 4px;
        background-color: #ffffff;
        text-align: center;
        color: #1a1a1a;
    }
    QProgressBar::chunk {
        border-radius: 3px;
    }
    
    /* Slider */
    QSlider::groove:horizontal {
        border: 1px solid #d4d4d4;
        height: 5px;
        background: #f5f5f5;
        border-radius: 3px;
    }
    QSlider::handle:horizontal {
        background: #0078d4;
        border: none;
        width: 15px;
        margin: -5px 0;
        border-radius: 7px;
    }
    QSlider::handle:horizontal:hover {
        background: #1084d8;
    }
)";

// --- 3. Ocean Dark (Cool Blue Dark Theme) ---
const QString Style_OceanDark = R"(
    /* Base */
    QMainWindow, QDialog, QMessageBox { 
        background-color: #0a1929; 
        color: #b2bac2; 
        font-family: "Segoe UI", -apple-system, BlinkMacSystemFont; 
        font-size: 14px; 
    }
    
    /* Lists */
    QListWidget { 
        background-color: #0d2238; 
        color: #b2bac2; 
        border: 1px solid #1e3a52; 
        border-radius: 8px; 
        font-size: 14px; 
        outline: none; 
        padding: 6px;
    }
    QListWidget::item { 
        padding: 11px 12px; 
        border-radius: 6px;
        margin: 1px 0px;
    }
    QListWidget::item:selected { 
        background-color: #0c4a6e; 
        color: #ffffff; 
        border-left: 3px solid #0ea5e9;
    }
    QListWidget::item:hover { 
        background-color: #133554; 
    }
    
    /* Inputs */
    QLineEdit { 
        background-color: #0d2238; 
        color: #e0f2fe; 
        border: 1px solid #1e3a52; 
        border-radius: 5px; 
        padding: 8px 11px;
        selection-background-color: #0c4a6e;
    }
    QLineEdit:focus { 
        border: 2px solid #0ea5e9; 
        background-color: #0f2942;
        padding: 7px 10px;
    }
    
    QTextEdit { 
        background-color: #0d2238; 
        color: #e0f2fe; 
        border: 1px solid #1e3a52; 
        border-radius: 5px; 
        padding: 8px;
        selection-background-color: #0c4a6e;
    }
    QTextEdit:focus { 
        border: 2px solid #0ea5e9;
        background-color: #0f2942;
        padding: 7px;
    }
    QLineEdit:read-only { 
        background-color: #0f1419; 
        color: #64748b; 
        border: 1px solid #1e3a52; 
    }
    
    QComboBox {
        background-color: #0d2238;
        color: #b2bac2;
        border: 1px solid #1e3a52;
        border-radius: 5px;
        padding: 7px 10px;
        padding-right: 25px;
    }
    QComboBox:hover {
        border: 1px solid #0ea5e9;
        background-color: #0f2942;
    }
    QComboBox:focus {
        border: 2px solid #0ea5e9;
        padding: 6px 9px;
        padding-right: 24px;
    }
    QComboBox::drop-down {
        border: none;
        width: 20px;
    }
    QComboBox::down-arrow {
        image: none;
        border-left: 4px solid transparent;
        border-right: 4px solid transparent;
        border-top: 5px solid #909090;
        margin-right: 5px;
    }
    QComboBox QAbstractItemView {
        background-color: #0d2238;
        color: #b2bac2;
        selection-background-color: #0c4a6e;
        border: 1px solid #1e3a52;
        border-radius: 4px;
    }
    
    /* Buttons */
    QPushButton { 
        background-color: #1e3a52; 
        color: #e0f2fe; 
        border: 1px solid #1e3a52; 
        border-radius: 5px; 
        padding: 8px 16px; 
        font-weight: 500;
    }
    QPushButton:hover { 
        background-color: #285a7a; 
        border: 1px solid #0ea5e9;
    }
    QPushButton:pressed { 
        background-color: #0c3a5e; 
    }
    QPushButton:disabled { 
        background-color: #0f1e2e; 
        color: #4a5568; 
        border: 1px solid #1a2332; 
    }
    
    QPushButton#NewButton { 
        background-color: #0369a1; 
        border: 1px solid #0369a1; 
        color: #ffffff;
    }
    QPushButton#NewButton:hover { 
        background-color: #0284c7; 
    }
    QPushButton#NewButton:pressed {
        background-color: #075985;
    }
    
    QPushButton#DeleteButton, QPushButton#RejectButton { 
        background-color: #b91c1c; 
        border: 1px solid #b91c1c; 
        color: #ffffff;
    }
    QPushButton#DeleteButton:hover { 
        background-color: #dc2626; 
    }
    QPushButton#DeleteButton:pressed {
        background-color: #991b1b;
    }
    
    QPushButton#InviteButton { 
        background-color: #ea580c; 
        border: 1px solid #ea580c; 
        color: #ffffff;
    }
    QPushButton#InviteButton:hover {
        background-color: #f97316;
    }
    
    QPushButton#IconButton { 
        background-color: transparent; 
        border: none; 
        padding: 8px; 
    }
    QPushButton#IconButton:hover { 
        background-color: #1e3a52; 
        border-radius: 6px; 
    }
    
    /* Text Labels */
    QLabel { 
        color: #b2bac2; 
    }
    QLabel#DialogTitle { 
        font-size: 20px; 
        font-weight: 700; 
        color: #f0f9ff; 
        margin-bottom: 8px; 
    }
    QLabel#DetailUsername { 
        font-size: 22px; 
        font-weight: 700; 
        color: #f0f9ff; 
    }
    QLabel#PlaceholderLabel { 
        font-size: 15px; 
        color: #64748b; 
        font-style: italic; 
    }
    QLabel#StatusLabel {
        font-size: 11px;
        color: #f59e0b;
    }
    QLabel#InviteInfoLabel {
        font-size: 16px;
        padding: 20px;
    }
    
    /* Menu */
    QMenuBar {
        background-color: #0f2436;
        color: #b2bac2;
        border-bottom: 1px solid #1e3a52;
    }
    QMenuBar::item {
        padding: 6px 12px;
        background-color: transparent;
    }
    QMenuBar::item:selected {
        background-color: #1e3a52;
    }
    
    QMenu { 
        background-color: #0d2238; 
        border: 1px solid #1e3a52; 
        color: #b2bac2; 
        border-radius: 4px;
        padding: 4px;
    }
    QMenu::item { 
        padding: 8px 32px 8px 16px;
        border-radius: 4px;
    }
    QMenu::item:selected { 
        background-color: #0c4a6e; 
        color: #ffffff; 
    }
    
    /* Splitter */
    QSplitter::handle { 
        background-color: #1e3a52; 
    }
    QSplitter::handle:hover {
        background-color: #285a7a;
    }
    
    /* Progress Bar */
    QProgressBar {
        border: 1px solid #1e3a52;
        border-radius: 4px;
        background-color: #0d2238;
        text-align: center;
    }
    QProgressBar::chunk {
        border-radius: 3px;
    }
    
    /* Slider */
    QSlider::groove:horizontal {
        border: 1px solid #1e3a52;
        height: 6px;
        background: #0d2238;
        border-radius: 3px;
    }
    QSlider::handle:horizontal {
        background: #0ea5e9;
        border: 1px solid #0ea5e9;
        width: 16px;
        margin: -6px 0;
        border-radius: 8px;
    }
    QSlider::handle:horizontal:hover {
        background: #38bdf8;
    }
    
    /* Checkbox */
    QCheckBox {
        color: #b2bac2;
        spacing: 8px;
    }
    QCheckBox::indicator {
        width: 17px;
        height: 17px;
        border: 1px solid #1e3a52;
        border-radius: 3px;
        background-color: #0d2238;
    }
    QCheckBox::indicator:hover {
        border: 1px solid #0ea5e9;
    }
    QCheckBox::indicator:checked {
        background-color: #0369a1;
        border: 1px solid #0369a1;
    }
)";

// --- 4. Warm Light (Cozy Light Theme) ---
const QString Style_WarmLight = R"(
    /* Base */
    QMainWindow, QDialog, QMessageBox { 
        background-color: #fef9f3; 
        color: #292524;
        font-family: "Segoe UI", -apple-system, BlinkMacSystemFont; 
        font-size: 14px; 
    }
    
    /* Lists */
    QListWidget { 
        background-color: #ffffff; 
        color: #292524; 
        border: 1px solid #e7e5e4; 
        border-radius: 8px; 
        font-size: 14px; 
        outline: none; 
        padding: 6px;
    }
    QListWidget::item { 
        padding: 11px 12px; 
        border-radius: 6px;
        margin: 1px 0px;
    }
    QListWidget::item:selected { 
        background-color: #fed7aa; 
        color: #7c2d12; 
        border-left: 3px solid #ea580c;
    }
    QListWidget::item:hover { 
        background-color: #fef3c7; 
    }
    
    /* Inputs */
    QLineEdit { 
        background-color: #ffffff; 
        color: #292524; 
        border: 1px solid #e7e5e4; 
        border-radius: 5px; 
        padding: 8px 11px;
        selection-background-color: #fed7aa;
    }
    QLineEdit:focus { 
        border: 2px solid #f97316; 
        background-color: #fffbf7;
        padding: 7px 10px;
    }
    
    QTextEdit { 
        background-color: #ffffff; 
        color: #292524; 
        border: 1px solid #e7e5e4; 
        border-radius: 5px; 
        padding: 8px;
        selection-background-color: #fed7aa;
    }
    QTextEdit:focus { 
        border: 2px solid #f97316; 
        background-color: #fffbf7;
        padding: 7px;
    }
    QLineEdit:read-only { 
        background-color: #fafaf9; 
        color: #78716c; 
        border: 1px solid #e7e5e4; 
    }
    
    QComboBox {
        background-color: #ffffff;
        color: #292524;
        border: 1px solid #e7e5e4;
        border-radius: 5px;
        padding: 7px 10px;
        padding-right: 25px;
    }
    QComboBox:hover {
        border: 1px solid #f97316;
        background-color: #fffbf7;
    }
    QComboBox:focus {
        border: 2px solid #f97316;
        padding: 6px 9px;
        padding-right: 24px;
    }
    QComboBox::drop-down {
        border: none;
        width: 20px;
    }
    QComboBox::down-arrow {
        image: none;
        border-left: 4px solid transparent;
        border-right: 4px solid transparent;
        border-top: 5px solid #78716c;
        margin-right: 5px;
    }
    QComboBox QAbstractItemView {
        background-color: #ffffff;
        color: #292524;
        selection-background-color: #fed7aa;
        border: 1px solid #e7e5e4;
        border-radius: 4px;
    }
    
    /* Buttons */
    QPushButton { 
        background-color: #fafaf9; 
        color: #292524; 
        border: 1px solid #e7e5e4; 
        border-radius: 5px; 
        padding: 8px 16px; 
        font-weight: 500;
    }
    QPushButton:hover { 
        background-color: #f5f5f4; 
        border: 1px solid #d6d3d1;
    }
    QPushButton:pressed { 
        background-color: #e7e5e4; 
    }
    QPushButton:disabled { 
        background-color: #fafaf9; 
        color: #a8a29e; 
        border: 1px solid #e7e5e4; 
    }
    
    QPushButton#NewButton { 
        background-color: #ea580c; 
        border: 1px solid #ea580c; 
        color: #ffffff;
    }
    QPushButton#NewButton:hover { 
        background-color: #f97316; 
    }
    QPushButton#NewButton:pressed {
        background-color: #c2410c;
    }
    
    QPushButton#DeleteButton, QPushButton#RejectButton { 
        background-color: #dc2626; 
        border: 1px solid #dc2626; 
        color: #ffffff;
    }
    QPushButton#DeleteButton:hover { 
        background-color: #ef4444; 
    }
    QPushButton#DeleteButton:pressed {
        background-color: #b91c1c;
    }
    
    QPushButton#InviteButton { 
        background-color: #d97706; 
        border: 1px solid #d97706; 
        color: #ffffff;
    }
    QPushButton#InviteButton:hover {
        background-color: #f59e0b;
    }
    
    QPushButton#IconButton { 
        background-color: transparent; 
        border: none; 
        padding: 8px; 
    }
    QPushButton#IconButton:hover { 
        background-color: #f5f5f4; 
        border-radius: 6px; 
    }
    
    /* Text Labels */
    QLabel { 
        color: #292524; 
    }
    QLabel#DialogTitle { 
        font-size: 20px; 
        font-weight: 700; 
        color: #1c1917; 
        margin-bottom: 8px; 
    }
    QLabel#DetailUsername { 
        font-size: 22px; 
        font-weight: 700; 
        color: #1c1917; 
    }
    QLabel#PlaceholderLabel { 
        font-size: 15px; 
        color: #a8a29e; 
        font-style: italic; 
    }
    QLabel#StatusLabel {
        font-size: 11px;
        color: #d97706;
    }
    QLabel#InviteInfoLabel {
        font-size: 16px;
        padding: 20px;
    }
    
    /* Menu */
    QMenuBar {
        background-color: #f5f5f4;
        color: #292524;
        border-bottom: 1px solid #e7e5e4;
    }
    QMenuBar::item {
        padding: 6px 12px;
        background-color: transparent;
    }
    QMenuBar::item:selected {
        background-color: #e7e5e4;
    }
    
    QMenu { 
        background-color: #ffffff; 
        border: 1px solid #e7e5e4; 
        color: #292524; 
        border-radius: 4px;
        padding: 4px;
    }
    QMenu::item { 
        padding: 8px 32px 8px 16px;
        border-radius: 4px;
    }
    QMenu::item:selected { 
        background-color: #fef3c7; 
        color: #292524; 
    }
    
    /* Splitter */
    QSplitter::handle { 
        background-color: #e7e5e4; 
    }
    QSplitter::handle:hover {
        background-color: #d6d3d1;
    }
    
    /* Progress Bar */
    QProgressBar {
        border: 1px solid #e7e5e4;
        border-radius: 4px;
        background-color: #fafaf9;
        text-align: center;
    }
    QProgressBar::chunk {
        border-radius: 3px;
    }
    
    /* Slider */
    QSlider::groove:horizontal {
        border: 1px solid #e7e5e4;
        height: 6px;
        background: #fafaf9;
        border-radius: 3px;
    }
    QSlider::handle:horizontal {
        background: #ea580c;
        border: 1px solid #ea580c;
        width: 16px;
        margin: -6px 0;
        border-radius: 8px;
    }
    QSlider::handle:horizontal:hover {
        background: #f97316;
    }
    
    /* Checkbox */
    QCheckBox {
        color: #292524;
        spacing: 8px;
    }
    QCheckBox::indicator {
        width: 17px;
        height: 17px;
        border: 1px solid #e7e5e4;
        border-radius: 3px;
        background-color: #ffffff;
    }
    QCheckBox::indicator:hover {
        border: 1px solid #f97316;
    }
    QCheckBox::indicator:checked {
        background-color: #ea580c;
        border: 1px solid #ea580c;
    }
)";

// --- 5. Vibrant Colors (Bright, Colorful Theme) ---
const QString Style_VibrantColors = R"(
    /* Base */
    QMainWindow, QDialog, QMessageBox { 
        background-color: #fff8f0; 
        color: #1f2937; 
        font-family: "Segoe UI", -apple-system, BlinkMacSystemFont; 
        font-size: 14px; 
    }
    
    /* Lists */
    QListWidget { 
        background-color: #ffffff; 
        color: #1f2937; 
        border: 1px solid #e5e7eb; 
        border-radius: 8px; 
        font-size: 14px; 
        outline: none; 
        padding: 6px;
    }
    QListWidget::item { 
        padding: 11px 12px; 
        border-radius: 6px;
        margin: 1px 0px;
    }
    QListWidget::item:selected { 
        background-color: #ec4899; 
        color: #ffffff; 
        font-weight: 600;
    }
    QListWidget::item:hover:!selected { 
        background-color: #f3f4f6; 
    }
    
    /* Standard Buttons */
    QPushButton { 
        background-color: #8b5cf6; 
        color: #ffffff; 
        border: none; 
        border-radius: 5px; 
        padding: 8px 16px; 
        font-size: 14px; 
        font-weight: 600;
    }
    QPushButton:hover { 
        background-color: #7c3aed; 
    }
    QPushButton:pressed { 
        background-color: #6d28d9; 
    }
    QPushButton:disabled { 
        background-color: #e5e7eb; 
        color: #9ca3af; 
        border: none;
    }
    
    /* Action Buttons */
    QPushButton#NewButton { 
        background-color: #10b981; 
        border: none;
        color: #ffffff;
        font-weight: 600;
    }
    QPushButton#NewButton:hover { 
        background-color: #059669; 
    }
    QPushButton#NewButton:pressed { 
        background-color: #047857; 
    }
    
    QPushButton#DeleteButton, QPushButton#RejectButton { 
        background-color: #ef4444; 
        border: none;
        color: #ffffff;
        font-weight: 600;
    }
    QPushButton#DeleteButton:hover, QPushButton#RejectButton:hover { 
        background-color: #dc2626; 
    }
    QPushButton#DeleteButton:pressed, QPushButton#RejectButton:pressed { 
        background-color: #b91c1c; 
    }
    
    QPushButton#InviteButton { 
        background-color: #f97316; 
        border: none;
        color: #ffffff;
        font-weight: 600;
    }
    QPushButton#InviteButton:hover { 
        background-color: #ea580c; 
    }
    QPushButton#InviteButton:pressed { 
        background-color: #c2410c; 
    }
    
    QPushButton#IconButton { 
        background-color: transparent; 
        border: none; 
        padding: 8px; 
    }
    QPushButton#IconButton:hover { 
        background-color: #f3f4f6; 
        border-radius: 6px; 
    }
    
    /* Line Edits & Text Edits */
    QLineEdit { 
        background-color: #ffffff; 
        color: #1f2937; 
        border: 1px solid #d1d5db; 
        border-radius: 5px; 
        padding: 8px 11px; 
        font-size: 14px; 
        selection-background-color: #fbbf24;
        selection-color: #1f2937;
    }
    QLineEdit:focus { 
        border: 2px solid #8b5cf6; 
        padding: 7px 10px;
    }
    QLineEdit:read-only { 
        background-color: #f3f4f6; 
        color: #6b7280; 
        border: 1px solid #e5e7eb; 
    }
    
    QTextEdit { 
        background-color: #ffffff; 
        color: #1f2937; 
        border: 1px solid #d1d5db; 
        border-radius: 5px; 
        padding: 8px; 
        font-size: 14px; 
        selection-background-color: #fbbf24;
        selection-color: #1f2937;
    }
    QTextEdit:focus { 
        border: 2px solid #8b5cf6;
        padding: 7px;
    }
    
    /* Text Labels */
    QLabel { 
        color: #1f2937; 
    }
    QLabel#DialogTitle { 
        font-size: 19px; 
        font-weight: 600; 
        color: #1f2937; 
        margin-bottom: 8px;
    }
    QLabel#DetailUsername { 
        font-size: 21px; 
        font-weight: 600; 
        color: #1f2937; 
    }
    QLabel#PlaceholderLabel { 
        font-size: 15px; 
        color: #6b7280; 
        font-style: italic; 
    }
    QLabel#StatusLabel { 
        font-size: 11px; 
        color: #f59e0b;
    }
    QLabel#InviteInfoLabel { 
        font-size: 16px; 
        padding: 20px;
    }
    
    /* ComboBox */
    QComboBox { 
        background-color: #ffffff; 
        color: #1f2937; 
        border: 1px solid #d1d5db; 
        border-radius: 5px; 
        padding: 7px 10px; 
        padding-right: 25px;
        font-size: 14px; 
    }
    QComboBox:hover { 
        border: 1px solid #a8a8a8; 
        background-color: #fafafa;
    }
    QComboBox:focus { 
        border: 2px solid #8b5cf6; 
        padding: 6px 9px;
        padding-right: 24px;
    }
    QComboBox::drop-down { 
        border: none; 
        width: 20px;
    }
    QComboBox::down-arrow { 
        image: none; 
        border-left: 4px solid transparent; 
        border-right: 4px solid transparent; 
        border-top: 5px solid #6b7280; 
        margin-right: 5px;
    }
    QComboBox QAbstractItemView { 
        background-color: #ffffff; 
        color: #1f2937; 
        border: 1px solid #d1d5db; 
        border-radius: 5px; 
        selection-background-color: #ec4899; 
        selection-color: #ffffff; 
        outline: none;
        padding: 4px;
    }
    QComboBox QAbstractItemView::item { 
        padding: 8px; 
        border-radius: 4px;
    }
    
    /* Checkbox */
    QCheckBox { 
        color: #1f2937; 
        spacing: 8px;
    }
    QCheckBox::indicator { 
        width: 17px; 
        height: 17px; 
        border: 1px solid #d1d5db; 
        border-radius: 3px; 
        background-color: #ffffff;
    }
    QCheckBox::indicator:hover { 
        border: 1px solid #a8a8a8; 
        background-color: #fafafa;
    }
    QCheckBox::indicator:checked { 
        background-color: #10b981; 
        border: 1px solid #10b981; 
        image: none;
    }
    
    /* Progress Bar */
    QProgressBar { 
        background-color: #e5e7eb; 
        border: none; 
        border-radius: 4px; 
        text-align: center; 
        height: 8px;
        color: #1f2937;
    }
    QProgressBar::chunk { 
        border-radius: 4px; 
        background-color: #8b5cf6;
    }
    
    /* Sliders */
    QSlider::groove:horizontal { 
        background-color: #e5e7eb; 
        height: 5px; 
        border-radius: 3px;
        border: 1px solid #d1d5db;
    }
    QSlider::handle:horizontal { 
        background-color: #8b5cf6; 
        width: 15px; 
        margin: -5px 0; 
        border-radius: 7px;
        border: none;
    }
    QSlider::handle:horizontal:hover { 
        background-color: #7c3aed; 
    }
    
    /* Menu Bar */
    QMenuBar { 
        background-color: #fff8f0; 
        color: #1f2937; 
        border-bottom: 1px solid #e5e7eb;
        padding: 2px 0px;
    }
    QMenuBar::item { 
        background-color: transparent; 
        padding: 6px 14px; 
        border-radius: 4px;
    }
    QMenuBar::item:selected { 
        background-color: #f3f4f6; 
    }
    
    /* Menu */
    QMenu { 
        background-color: #ffffff; 
        color: #1f2937; 
        border: 1px solid #d1d5db; 
        border-radius: 6px; 
        padding: 6px;
    }
    QMenu::item { 
        padding: 8px 30px 8px 14px; 
        border-radius: 4px;
    }
    QMenu::item:selected { 
        background-color: #ec4899; 
        color: #ffffff; 
    }
    QMenu::separator { 
        height: 1px; 
        background-color: #e5e7eb; 
        margin: 4px 8px;
    }
    
    /* Splitter */
    QSplitter::handle { 
        background-color: #e5e7eb; 
    }
    QSplitter::handle:hover { 
        background-color: #d1d5db; 
    }
)";

// --- REGISTRY ---
static const QList<ThemeDef> Available = {
    { 
        "professional", 
        "Professional Slate", 
        Style_Professional, 
        QColor("#ffffff"), // White Icons (for Dark BG)
        QColor("#e0e0e0")  // Light Grey UI Icons
    },
    { 
        "light", 
        "Modern Light", 
        Style_ModernLight, 
        QColor("#1f2937"), // Dark Icons (for Light BG)
        QColor("#4b5563")  // Grey UI Icons
    },
    { 
        "ocean", 
        "Ocean Dark", 
        Style_OceanDark, 
        QColor("#e0f2fe"), // Light Blue Icons (for Dark BG)
        QColor("#b2bac2")  // Cool Grey UI Icons
    },
    { 
        "warm", 
        "Warm Light", 
        Style_WarmLight, 
        QColor("#292524"), // Dark Brown Icons (for Light BG)
        QColor("#57534e")  // Warm Grey UI Icons
    },
    { 
        "vibrant", 
        "Vibrant Colors", 
        Style_VibrantColors, 
        QColor("#1f2937"), // Dark Icons (for Light BG)
        QColor("#4b5563")  // Grey UI Icons
    }
};

// Helpers
inline const ThemeDef& getDefault() { return Available[0]; }

inline const ThemeDef& getById(const QString& id) {
    for(const auto& t : Available) {
        if(t.id == id) return t;
    }
    return getDefault();
}

}
}