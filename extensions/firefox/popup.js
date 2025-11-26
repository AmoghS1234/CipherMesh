// Popup script for CipherMesh extension

// Check connection status
browser.runtime.sendMessage({ type: "CHECK_CONNECTION" }).then(response => {
    updateStatus(response.connected);
}).catch(() => {
    updateStatus(false);
});

function updateStatus(connected) {
    const statusDiv = document.getElementById('status');
    
    if (connected) {
        statusDiv.className = 'status connected';
        statusDiv.innerHTML = '<span class="status-icon">✓</span><span>Connected to CipherMesh</span>';
    } else {
        statusDiv.className = 'status disconnected';
        statusDiv.innerHTML = '<span class="status-icon">✗</span><span>Not connected to app</span>';
    }
}

// Open app button (placeholder - native messaging doesn't support this directly)
document.getElementById('openApp').addEventListener('click', () => {
    alert('Please ensure the vault service is properly configured.');
});
