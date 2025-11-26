// Content script for CipherMesh Chrome Extension
// Detects password fields and injects auto-fill UI

(function() {
    'use strict';
    
    const CIPHERMESH_MARKER = 'data-ciphermesh-processed';
    let isConnected = false;
    
    // Initialize on load
    function initialize() {
        console.log('[CipherMesh] Initializing content script...');
        
        // Check for pending credentials from a previous login attempt
        checkPendingCredentials();
        
        // Always scan for password fields immediately, regardless of connection
        console.log('[CipherMesh] Scanning for password fields...');
        scanForPasswordFields();
        setupMutationObserver();
        
        // Check connection status in background
        chrome.runtime.sendMessage({ type: "CHECK_CONNECTION" }).then(response => {
            console.log('[CipherMesh] Connection check result:', response);
            isConnected = response.connected;
            if (isConnected) {
                console.log('[CipherMesh] Connected to vault service');
            } else {
                console.log('[CipherMesh] Not connected to vault service - buttons will still appear but may not function');
            }
        }).catch((error) => {
            console.error('[CipherMesh] Connection check failed:', error);
            isConnected = false;
            console.log('[CipherMesh] Buttons will appear but vault service connection required for functionality');
        });
    }
    
    // Check for pending credentials from a previous login attempt
    async function checkPendingCredentials() {
        const pendingStr = sessionStorage.getItem('ciphermesh-pending-credentials');
        if (!pendingStr) {
            console.log('[CipherMesh] No pending credentials to check');
            return;
        }
        
        try {
            const pending = JSON.parse(pendingStr);
            console.log('[CipherMesh] Found pending credentials for:', pending.username, 'on', pending.url);
            
            // Check if credentials are still fresh (within 30 seconds)
            const age = Date.now() - pending.timestamp;
            if (age > 30000) {
                console.log('[CipherMesh] Pending credentials expired (age:', age, 'ms)');
                sessionStorage.removeItem('ciphermesh-pending-credentials');
                return;
            }
            
            // Check if we're on a different page (indicates successful login)
            // If we're still on the login page with a password field, the login likely failed
            const currentUrl = window.location.hostname;
            const hasPasswordField = document.querySelector('input[type="password"]');
            const onSamePage = currentUrl === pending.url;
            
            // Heuristic: If we're on a different page OR we're on the same domain but no password field, 
            // assume login succeeded. If we're on the exact same page with a password field, login likely failed.
            const loginSucceeded = !hasPasswordField || window.location.href !== pending.fullUrl;
            
            if (!loginSucceeded) {
                console.log('[CipherMesh] Login appears to have failed (still on login page with password field)');
                // Keep credentials for retry - user might try again
                return;
            }
            
            // Clear pending credentials immediately to prevent duplicate prompts
            sessionStorage.removeItem('ciphermesh-pending-credentials');
            
            console.log('[CipherMesh] Login appears successful, checking if credentials already exist...');
            
            // Check if these credentials already exist in the vault
            try {
                const response = await chrome.runtime.sendMessage({
                    type: "GET_CREDENTIALS",
                    url: pending.url,
                    username: pending.username
                });
                
                console.log('[CipherMesh] Credential check response:', response);
                
                if (response.success && response.data) {
                    const entries = response.data.entries || response.data.credentials || [];
                    
                    // Check if any entry matches this username
                    const existingEntry = entries.find(e => e.username === pending.username);
                    
                    if (existingEntry) {
                        console.log('[CipherMesh] Credentials already exist in vault for this username, not prompting');
                        return;
                    }
                }
                
                // Credentials don't exist - prompt to save
                console.log('[CipherMesh] Credentials not found in vault, prompting to save');
                
                // Small delay to let the page settle
                setTimeout(async () => {
                    const shouldSave = await showConfirmDialog(
                        `Save password for <strong>${pending.username}</strong> on <strong>${pending.url}</strong> to your CipherMesh vault?`,
                        'Save Password'
                    );
                    
                    if (shouldSave) {
                        console.log('[CipherMesh] User chose to save password');
                        await promptSaveCredentials(pending.url, pending.username, pending.password);
                    } else {
                        console.log('[CipherMesh] User declined to save password');
                    }
                }, 500);
                
            } catch (error) {
                console.error('[CipherMesh] Error checking credentials:', error);
                // On error, still offer to save (vault might not be unlocked)
                setTimeout(async () => {
                    const shouldSave = await showConfirmDialog(
                        `Save password for <strong>${pending.username}</strong> on <strong>${pending.url}</strong> to your CipherMesh vault?`,
                        'Save Password'
                    );
                    
                    if (shouldSave) {
                        await promptSaveCredentials(pending.url, pending.username, pending.password);
                    }
                }, 500);
            }
            
        } catch (error) {
            console.error('[CipherMesh] Error parsing pending credentials:', error);
            sessionStorage.removeItem('ciphermesh-pending-credentials');
        }
    }
    
    // Run immediately if DOM is ready, otherwise wait
    if (document.readyState === 'loading') {
        console.log('[CipherMesh] DOM still loading, waiting...');
        document.addEventListener('DOMContentLoaded', initialize);
    } else {
        console.log('[CipherMesh] DOM ready, initializing immediately');
        initialize();
    }
    
    // Listen for connection status updates
    chrome.runtime.onMessage.addListener((message) => {
        if (message.type === "CONNECTION_STATUS") {
            isConnected = message.connected;
            if (isConnected) {
                scanForPasswordFields();
            }
        }
    });
    
    // Setup MutationObserver to detect dynamically added password fields
    function setupMutationObserver() {
        let debounceTimer = null;
        
        const observer = new MutationObserver((mutations) => {
            let shouldScan = false;
            
            for (const mutation of mutations) {
                if (mutation.addedNodes.length > 0) {
                    for (const node of mutation.addedNodes) {
                        if (node.nodeType === 1) { // Element node
                            if (node.tagName === 'INPUT' || node.querySelector) {
                                shouldScan = true;
                                break;
                            }
                        }
                    }
                }
                if (shouldScan) break;
            }
            
            if (shouldScan) {
                // Debounce scanning to avoid excessive re-processing
                if (debounceTimer) {
                    clearTimeout(debounceTimer);
                }
                debounceTimer = setTimeout(() => {
                    console.log('[CipherMesh] Mutation detected, rescanning...');
                    scanForPasswordFields();
                }, 250); // Wait 250ms after last mutation
            }
        });
        
        observer.observe(document.body, {
            childList: true,
            subtree: true
        });
    }
    
    // Detect password fields on the page
    function scanForPasswordFields() {
        const passwordFields = document.querySelectorAll('input[type="password"]');
        console.log('[CipherMesh] Found', passwordFields.length, 'password fields');
        
        let processed = 0;
        passwordFields.forEach(field => {
            // Skip password fields that are part of CipherMesh's own UI (like master password dialog)
            if (field.id && field.id.startsWith('ciphermesh-')) {
                console.log('[CipherMesh] Skipping CipherMesh UI field:', field.id);
                return;
            }
            // Skip if field is inside a CipherMesh modal
            if (field.closest('.ciphermesh-modal-overlay') || field.closest('.ciphermesh-modal')) {
                console.log('[CipherMesh] Skipping field inside CipherMesh modal');
                return;
            }
            
            if (!field.hasAttribute(CIPHERMESH_MARKER) && isVisible(field)) {
                field.setAttribute(CIPHERMESH_MARKER, 'true');
                processPasswordField(field);
                processed++;
            }
        });
        console.log('[CipherMesh] Processed', processed, 'new password fields');
    }
    
    // Check if element is visible
    function isVisible(element) {
        return element.offsetWidth > 0 && element.offsetHeight > 0 &&
               window.getComputedStyle(element).visibility !== 'hidden' &&
               window.getComputedStyle(element).display !== 'none';
    }
    
    // Process individual password field
    function processPasswordField(passwordField) {
        const form = passwordField.closest('form');
        
        // Find username field - look in form if available, otherwise in page
        const usernameField = form 
            ? findUsernameField(form, passwordField)
            : findUsernameFieldInPage(passwordField);
        
        // Track manual typing vs autofill
        // Set flag to false initially - will be set true by autofill, reset to false by typing
        passwordField.dataset.ciphermeshAutofilled = 'false';
        
        // Listen for manual typing to reset the autofill flag
        passwordField.addEventListener('input', (e) => {
            // If the user is typing (inputType exists), it's manual input
            if (e.inputType) {
                passwordField.dataset.ciphermeshAutofilled = 'false';
            }
        });
        
        // Add auto-fill button (always add, even without form)
        addAutoFillButton(passwordField, usernameField);
        
        // Create a handler function that stores credentials before form submission
        const handleSubmitAttempt = (e) => {
            const username = usernameField ? usernameField.value : '';
            const password = passwordField.value;
            const url = window.location.hostname;
            const fullUrl = window.location.href;
            
            console.log('[CipherMesh] Submit attempt detected - username:', username, 'password length:', password.length);
            
            // Check if we have valid credentials to potentially save
            if (!username || !password) {
                console.log('[CipherMesh] Missing username or password, not storing for save check');
                return;
            }
            
            // Don't store if this was autofilled
            if (passwordField.dataset && passwordField.dataset.ciphermeshAutofilled === 'true') {
                console.log('[CipherMesh] Password was autofilled, not storing for save check');
                return;
            }
            
            // Store credentials in sessionStorage for checking after navigation
            // We'll check after the login completes whether to prompt to save
            const pendingCredentials = {
                url: url,
                fullUrl: fullUrl,
                username: username,
                password: password,
                timestamp: Date.now()
            };
            
            console.log('[CipherMesh] Storing credentials for post-login check');
            sessionStorage.setItem('ciphermesh-pending-credentials', JSON.stringify(pendingCredentials));
            
            // Allow normal form submission - don't prevent default
            // The save prompt will appear after successful login
        };
        
        // Listen for form submission to capture credentials (only if in a form)
        if (form) {
            // Use capturing phase to catch the event before any other handlers
            form.addEventListener('submit', handleSubmitAttempt, true);
            
            // Also intercept clicks on submit buttons within the form
            const submitButtons = form.querySelectorAll('button[type="submit"], input[type="submit"], button:not([type])');
            submitButtons.forEach(button => {
                button.addEventListener('click', handleSubmitAttempt, true);
            });
            
            // Also intercept Enter key in form fields
            form.addEventListener('keydown', (e) => {
                if (e.key === 'Enter' && (e.target === passwordField || e.target === usernameField)) {
                    console.log('[CipherMesh] Enter key pressed in form field');
                    handleSubmitAttempt(e);
                }
            }, true);
        } else {
            // For password fields not in forms, try to detect login buttons or Enter key
            passwordField.addEventListener('keydown', (e) => {
                if (e.key === 'Enter') {
                    console.log('[CipherMesh] Enter key pressed (no form)');
                    storeAndHandlePasswordSubmit(usernameField, passwordField);
                }
            });
            
            // Also try to find and watch login/submit buttons near the password field
            watchNearbyButtons(passwordField, usernameField);
        }
    }
    
    // Store credentials and handle password submission for fields not in forms
    function storeAndHandlePasswordSubmit(usernameField, passwordField) {
        const username = usernameField ? usernameField.value : '';
        const password = passwordField.value;
        const url = window.location.hostname;
        const fullUrl = window.location.href;
        
        console.log('[CipherMesh] Password submit detected (no form) - username:', username);
        
        if (!username || !password) {
            console.log('[CipherMesh] Missing username or password, skipping');
            return;
        }
        
        // Don't store if this was autofilled
        if (passwordField.dataset.ciphermeshAutofilled === 'true') {
            console.log('[CipherMesh] Password was autofilled, not storing');
            return;
        }
        
        // Store credentials for post-login check
        const pendingCredentials = {
            url: url,
            fullUrl: fullUrl,
            username: username,
            password: password,
            timestamp: Date.now()
        };
        
        console.log('[CipherMesh] Storing credentials for post-login check (no form)');
        sessionStorage.setItem('ciphermesh-pending-credentials', JSON.stringify(pendingCredentials));
    }
    
    // Watch for clicks on nearby login buttons (for pages without forms)
    function watchNearbyButtons(passwordField, usernameField) {
        const parent = passwordField.closest('div, section, article') || passwordField.parentElement;
        if (!parent) return;
        
        const buttons = parent.querySelectorAll('button, input[type="submit"], input[type="button"], a[role="button"]');
        buttons.forEach(btn => {
            const text = (btn.textContent || btn.value || '').toLowerCase();
            if (text.match(/log\s*in|sign\s*in|submit|continue|next/)) {
                btn.addEventListener('click', () => {
                    storeAndHandlePasswordSubmit(usernameField, passwordField);
                });
            }
        });
    }
    
    // Handle password submission for fields not in forms (legacy function, kept for compatibility)
    function handlePasswordSubmit(usernameField, passwordField) {
        storeAndHandlePasswordSubmit(usernameField, passwordField);
    }
    
    // Find username field in page (for password fields not in forms)
    function findUsernameFieldInPage(passwordField) {
        // Look for common username/email inputs near the password field
        const allInputs = document.querySelectorAll('input[type="text"], input[type="email"]');
        for (const input of allInputs) {
            const name = (input.name || '').toLowerCase();
            const id = (input.id || '').toLowerCase();
            if (name.match(/user|email|login|account/) || id.match(/user|email|login|account/)) {
                return input;
            }
        }
        return null;
    }
    
    // Find username field in form
    function findUsernameField(form, passwordField) {
        const inputs = Array.from(form.querySelectorAll('input[type="text"], input[type="email"], input'));
        
        // Find input before password field
        const passwordIndex = inputs.indexOf(passwordField);
        for (let i = passwordIndex - 1; i >= 0; i--) {
            const input = inputs[i];
            const type = input.type.toLowerCase();
            if (type === 'text' || type === 'email' || 
                input.name.match(/user|email|login/i) ||
                input.id.match(/user|email|login/i)) {
                return input;
            }
        }
        
        // Fallback: any text/email input
        return inputs.find(input => {
            const type = input.type.toLowerCase();
            return type === 'text' || type === 'email';
        });
    }
    
    // Add auto-fill button next to password field
    function addAutoFillButton(passwordField, usernameField) {
        // Check if already has button
        if (passwordField.parentElement.querySelector('.ciphermesh-autofill-btn')) {
            console.log('[CipherMesh] Button already exists for this field');
            return;
        }
        
        console.log('[CipherMesh] Adding autofill button to password field');
        
        // Calculate button position to avoid conflicts with browser's show/hide button
        const computedStyle = window.getComputedStyle(passwordField);
        const paddingRight = parseInt(computedStyle.paddingRight) || 0;
        
        // Browser show/hide buttons are typically ~30px wide and positioned at right: 5-10px
        // We position our button to the LEFT of any existing button
        // If padding > 30, browser likely has a show/hide button, so we offset more
        const browserButtonWidth = 35;
        const rightOffset = paddingRight > 30 ? paddingRight + 5 : 40; // Always offset at least 40px to avoid overlap
        
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'ciphermesh-autofill-btn';
        button.innerHTML = '🔐';
        button.title = 'Auto-fill with CipherMesh';
        button.setAttribute('aria-label', 'Auto-fill password with CipherMesh');
        button.style.cssText = `
            position: absolute;
            right: ${rightOffset}px;
            top: 50%;
            transform: translateY(-50%);
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            border: none;
            border-radius: 6px;
            padding: 6px 10px;
            cursor: pointer;
            font-size: 16px;
            z-index: 10000;
            transition: all 0.2s;
            box-shadow: 0 2px 4px rgba(102, 126, 234, 0.3);
            line-height: 1;
            display: flex;
            align-items: center;
            justify-content: center;
        `;
        
        button.addEventListener('mouseenter', () => {
            button.style.background = 'linear-gradient(135deg, #764ba2 0%, #667eea 100%)';
            button.style.transform = 'translateY(-50%) scale(1.05)';
            button.style.boxShadow = '0 4px 8px rgba(102, 126, 234, 0.4)';
        });
        
        button.addEventListener('mouseleave', () => {
            button.style.background = 'linear-gradient(135deg, #667eea 0%, #764ba2 100%)';
            button.style.transform = 'translateY(-50%)';
            button.style.boxShadow = '0 2px 4px rgba(102, 126, 234, 0.3)';
        });
        
        button.addEventListener('click', async (e) => {
            e.preventDefault();
            e.stopPropagation();
            e.stopImmediatePropagation();
            console.log('[CipherMesh] Autofill button clicked');
            await handleAutoFill(passwordField, usernameField);
        });
        
        // Make parent relative if not already positioned
        const parent = passwordField.parentElement;
        const position = window.getComputedStyle(parent).position;
        if (position === 'static') {
            parent.style.position = 'relative';
        }
        
        // Adjust password field padding to make room for both buttons
        // Our button is ~35px wide, plus we need space for browser's button if present
        const totalPaddingNeeded = paddingRight > 30 ? paddingRight + 45 : 80;
        if (paddingRight < totalPaddingNeeded) {
            passwordField.style.paddingRight = `${totalPaddingNeeded}px`;
        }
        
        parent.appendChild(button);
        console.log('[CipherMesh] Button added successfully');
    }
    
    // Create styled modal dialog
    function createModal(title, content, buttons) {
        const overlay = document.createElement('div');
        overlay.className = 'ciphermesh-modal-overlay';
        overlay.style.cssText = `
            position: fixed !important;
            top: 0 !important;
            left: 0 !important;
            width: 100% !important;
            height: 100% !important;
            background: rgba(0, 0, 0, 0.5) !important;
            z-index: 2147483647 !important;
            display: flex !important;
            align-items: center !important;
            justify-content: center !important;
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif !important;
            margin: 0 !important;
            padding: 0 !important;
            border: none !important;
            box-sizing: border-box !important;
        `;
        
        const modal = document.createElement('div');
        modal.className = 'ciphermesh-modal';
        modal.style.cssText = `
            background: white !important;
            border-radius: 12px !important;
            box-shadow: 0 10px 40px rgba(0, 0, 0, 0.3) !important;
            padding: 0 !important;
            max-width: 450px !important;
            width: 90% !important;
            animation: ciphermesh-modal-appear 0.2s ease-out !important;
            margin: 0 !important;
            border: none !important;
            box-sizing: border-box !important;
            color: #333 !important;
            font-size: 14px !important;
            line-height: 1.5 !important;
            text-align: left !important;
        `;
        
        const header = document.createElement('div');
        header.style.cssText = `
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%) !important;
            color: white !important;
            padding: 20px 24px !important;
            border-radius: 12px 12px 0 0 !important;
            font-size: 18px !important;
            font-weight: 600 !important;
            display: flex !important;
            align-items: center !important;
            gap: 10px !important;
            margin: 0 !important;
            border: none !important;
            box-sizing: border-box !important;
            line-height: 1.4 !important;
        `;
        header.innerHTML = `🔐 ${title}`;
        
        const body = document.createElement('div');
        body.style.cssText = `
            padding: 24px !important;
            color: #333 !important;
            background: white !important;
            margin: 0 !important;
            border: none !important;
            box-sizing: border-box !important;
        `;
        body.appendChild(content);
        
        const footer = document.createElement('div');
        footer.style.cssText = `
            padding: 16px 24px !important;
            background: #f8f9fa !important;
            border-radius: 0 0 12px 12px !important;
            display: flex !important;
            gap: 12px !important;
            justify-content: flex-end !important;
            margin: 0 !important;
            border: none !important;
            box-sizing: border-box !important;
        `;
        
        buttons.forEach(btn => footer.appendChild(btn));
        
        modal.appendChild(header);
        modal.appendChild(body);
        modal.appendChild(footer);
        overlay.appendChild(modal);
        
        // Add animation
        const style = document.createElement('style');
        style.textContent = `
            @keyframes ciphermesh-modal-appear {
                from {
                    opacity: 0;
                    transform: scale(0.9) translateY(-20px);
                }
                to {
                    opacity: 1;
                    transform: scale(1) translateY(0);
                }
            }
        `;
        document.head.appendChild(style);
        
        return overlay;
    }
    
    // Show master password input dialog
    function showMasterPasswordDialog() {
        return new Promise((resolve) => {
            const content = document.createElement('div');
            content.style.cssText = 'all: initial !important; display: block !important;';
            
            const description = document.createElement('p');
            description.textContent = 'Enter your CipherMesh master password to continue';
            description.style.cssText = `
                all: initial !important;
                display: block !important;
                margin: 0 0 16px 0 !important;
                color: #666 !important;
                font-size: 14px !important;
                line-height: 1.5 !important;
                padding: 0 !important;
                background: transparent !important;
                border: none !important;
                font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif !important;
            `;
            content.appendChild(description);
            
            const input = document.createElement('input');
            input.type = 'password';
            input.id = 'ciphermesh-master-pwd';
            input.placeholder = 'Master password';
            input.style.cssText = `
                all: initial !important;
                display: block !important;
                width: 100% !important;
                padding: 12px 16px !important;
                border: 2px solid #e0e0e0 !important;
                border-radius: 8px !important;
                font-size: 15px !important;
                box-sizing: border-box !important;
                transition: border-color 0.2s !important;
                font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif !important;
                background: white !important;
                background-color: white !important;
                color: #333 !important;
                margin: 0 !important;
                outline: none !important;
                height: auto !important;
                min-height: 44px !important;
                line-height: 1.5 !important;
                -webkit-appearance: none !important;
                -moz-appearance: none !important;
                appearance: none !important;
                box-shadow: none !important;
                text-indent: 0 !important;
                text-align: left !important;
                letter-spacing: normal !important;
                word-spacing: normal !important;
                text-transform: none !important;
                text-shadow: none !important;
                cursor: text !important;
            `;
            content.appendChild(input);
            
            input.addEventListener('focus', () => {
                input.style.setProperty('border-color', '#667eea', 'important');
                input.style.setProperty('outline', 'none', 'important');
                input.style.setProperty('box-shadow', '0 0 0 3px rgba(102, 126, 234, 0.1)', 'important');
            });
            input.addEventListener('blur', () => {
                input.style.setProperty('border-color', '#e0e0e0', 'important');
                input.style.setProperty('box-shadow', 'none', 'important');
            });
            
            const okButton = document.createElement('button');
            okButton.textContent = 'Unlock';
            okButton.style.cssText = `
                all: initial !important;
                display: inline-block !important;
                padding: 10px 24px !important;
                background: linear-gradient(135deg, #667eea 0%, #764ba2 100%) !important;
                color: white !important;
                border: none !important;
                border-radius: 6px !important;
                font-size: 14px !important;
                font-weight: 600 !important;
                cursor: pointer !important;
                transition: transform 0.1s, box-shadow 0.2s !important;
                margin: 0 !important;
                font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif !important;
                line-height: 1.5 !important;
            `;
            okButton.addEventListener('mouseenter', () => {
                okButton.style.setProperty('transform', 'translateY(-1px)', 'important');
                okButton.style.setProperty('box-shadow', '0 4px 12px rgba(102, 126, 234, 0.4)', 'important');
            });
            okButton.addEventListener('mouseleave', () => {
                okButton.style.setProperty('transform', 'translateY(0)', 'important');
                okButton.style.setProperty('box-shadow', 'none', 'important');
            });
            
            const cancelButton = document.createElement('button');
            cancelButton.textContent = 'Cancel';
            cancelButton.style.cssText = `
                all: initial !important;
                display: inline-block !important;
                padding: 10px 24px !important;
                background: white !important;
                background-color: white !important;
                color: #666 !important;
                border: 2px solid #e0e0e0 !important;
                border-radius: 6px !important;
                font-size: 14px !important;
                font-weight: 600 !important;
                cursor: pointer !important;
                transition: all 0.2s !important;
                margin: 0 !important;
                font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif !important;
                line-height: 1.5 !important;
            `;
            cancelButton.addEventListener('mouseenter', () => {
                cancelButton.style.setProperty('border-color', '#999', 'important');
                cancelButton.style.setProperty('color', '#333', 'important');
            });
            cancelButton.addEventListener('mouseleave', () => {
                cancelButton.style.setProperty('border-color', '#e0e0e0', 'important');
                cancelButton.style.setProperty('color', '#666', 'important');
            });
            
            const modal = createModal('Master Password', content, [cancelButton, okButton]);
            
            const submit = () => {
                const password = input.value;
                document.body.removeChild(modal);
                resolve(password);
            };
            
            okButton.addEventListener('click', submit);
            cancelButton.addEventListener('click', () => {
                document.body.removeChild(modal);
                resolve(null);
            });
            
            input.addEventListener('keypress', (e) => {
                if (e.key === 'Enter') submit();
            });
            
            document.body.appendChild(modal);
            setTimeout(() => input.focus(), 100);
        });
    }
    
    // Show confirmation dialog
    function showConfirmDialog(message, title = 'Confirm') {
        return new Promise((resolve) => {
            const content = document.createElement('div');
            content.style.cssText = 'all: initial !important; display: block !important;';
            
            const para = document.createElement('p');
            para.innerHTML = message;
            para.style.cssText = `
                all: initial !important;
                display: block !important;
                margin: 0 !important;
                color: #555 !important;
                font-size: 15px !important;
                line-height: 1.5 !important;
                font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif !important;
            `;
            content.appendChild(para);
            
            const yesButton = document.createElement('button');
            yesButton.textContent = 'Yes';
            yesButton.style.cssText = `
                all: initial !important;
                display: inline-block !important;
                padding: 10px 24px !important;
                background: linear-gradient(135deg, #667eea 0%, #764ba2 100%) !important;
                color: white !important;
                border: none !important;
                border-radius: 6px !important;
                font-size: 14px !important;
                font-weight: 600 !important;
                cursor: pointer !important;
                transition: transform 0.1s, box-shadow 0.2s !important;
                font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif !important;
                line-height: 1.5 !important;
            `;
            yesButton.addEventListener('mouseenter', () => {
                yesButton.style.setProperty('transform', 'translateY(-1px)', 'important');
                yesButton.style.setProperty('box-shadow', '0 4px 12px rgba(102, 126, 234, 0.4)', 'important');
            });
            yesButton.addEventListener('mouseleave', () => {
                yesButton.style.setProperty('transform', 'translateY(0)', 'important');
                yesButton.style.setProperty('box-shadow', 'none', 'important');
            });
            
            const noButton = document.createElement('button');
            noButton.textContent = 'No';
            noButton.style.cssText = `
                all: initial !important;
                display: inline-block !important;
                padding: 10px 24px !important;
                background: white !important;
                background-color: white !important;
                color: #666 !important;
                border: 2px solid #e0e0e0 !important;
                border-radius: 6px !important;
                font-size: 14px !important;
                font-weight: 600 !important;
                cursor: pointer !important;
                transition: all 0.2s !important;
                font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif !important;
                line-height: 1.5 !important;
            `;
            noButton.addEventListener('mouseenter', () => {
                noButton.style.setProperty('border-color', '#999', 'important');
                noButton.style.setProperty('color', '#333', 'important');
            });
            noButton.addEventListener('mouseleave', () => {
                noButton.style.setProperty('border-color', '#e0e0e0', 'important');
                noButton.style.setProperty('color', '#666', 'important');
            });
            
            const modal = createModal(title, content, [noButton, yesButton]);
            
            yesButton.addEventListener('click', () => {
                document.body.removeChild(modal);
                resolve(true);
            });
            
            noButton.addEventListener('click', () => {
                document.body.removeChild(modal);
                resolve(false);
            });
            
            document.body.appendChild(modal);
        });
    }
    
    // Show alert dialog
    function showAlertDialog(message, title = 'CipherMesh', isError = false) {
        return new Promise((resolve) => {
            const content = document.createElement('div');
            content.innerHTML = `
                <p style="margin: 0; color: #555; font-size: 15px; line-height: 1.5;">
                    ${message}
                </p>
            `;
            
            const okButton = document.createElement('button');
            okButton.textContent = 'OK';
            okButton.style.cssText = `
                padding: 10px 32px;
                background: ${isError ? 'linear-gradient(135deg, #f093fb 0%, #f5576c 100%)' : 'linear-gradient(135deg, #667eea 0%, #764ba2 100%)'};
                color: white;
                border: none;
                border-radius: 6px;
                font-size: 14px;
                font-weight: 600;
                cursor: pointer;
                transition: transform 0.1s, box-shadow 0.2s;
            `;
            okButton.addEventListener('mouseenter', () => {
                okButton.style.transform = 'translateY(-1px)';
                okButton.style.boxShadow = `0 4px 12px ${isError ? 'rgba(245, 87, 108, 0.4)' : 'rgba(102, 126, 234, 0.4)'}`;
            });
            okButton.addEventListener('mouseleave', () => {
                okButton.style.transform = 'translateY(0)';
                okButton.style.boxShadow = 'none';
            });
            
            const modal = createModal(title, content, [okButton]);
            
            okButton.addEventListener('click', () => {
                document.body.removeChild(modal);
                resolve();
            });
            
            document.body.appendChild(modal);
            okButton.focus();
        });
    }
    
    // Handle auto-fill button click
    async function handleAutoFill(passwordField, usernameField) {
        console.log('[CipherMesh] Starting autofill process...');
        const url = window.location.hostname;
        const username = usernameField ? usernameField.value : '';
        console.log('[CipherMesh] URL:', url, 'Username:', username || '(empty)');
        
        // Request master password with styled dialog
        console.log('[CipherMesh] Requesting master password...');
        const masterPassword = await showMasterPasswordDialog();
        if (!masterPassword) {
            console.log('[CipherMesh] Master password dialog canceled');
            return;
        }
        
        console.log('[CipherMesh] Verifying master password...');
        // Verify master password first
        try {
            const verifyResponse = await chrome.runtime.sendMessage({
                type: "VERIFY_MASTER_PASSWORD",
                password: masterPassword
            });
            
            console.log('[CipherMesh] Verify response:', verifyResponse);
            
            if (!verifyResponse || !verifyResponse.success) {
                console.error('[CipherMesh] Verification failed:', verifyResponse);
                await showAlertDialog('Failed to verify password. The password may be incorrect.', 'Verification Failed', true);
                return;
            }
            
            if (!verifyResponse.verified) {
                console.log('[CipherMesh] Password incorrect');
                await showAlertDialog('The master password you entered is incorrect.', 'Incorrect Password', true);
                return;
            }
            
            console.log('[CipherMesh] Password verified successfully');
        } catch (error) {
            console.error('[CipherMesh] Error verifying password:', error);
            await showAlertDialog('Failed to verify password: ' + error.message, 'Error', true);
            return;
        }
        
        // Get credentials
        console.log('[CipherMesh] Fetching credentials...');
        try {
            const response = await chrome.runtime.sendMessage({
                type: "GET_CREDENTIALS",
                url: url,
                username: username
            });
            
            console.log('[CipherMesh] Credentials response:', response);
            
            if (response.success && response.data) {
                // Check for both 'entries' and 'credentials' fields (vault-service uses 'credentials' for multiple)
                const entries = response.data.entries || response.data.credentials || [];
                console.log('[CipherMesh] Found', entries.length, 'entries');
                
                if (entries.length === 0) {
                    await showAlertDialog('No credentials found for this site in your vault.', 'No Credentials');
                } else if (entries.length === 1) {
                    console.log('[CipherMesh] Filling credentials from single entry');
                    fillCredentials(entries[0], usernameField, passwordField);
                    await showAlertDialog('Credentials filled successfully!', 'Success');
                } else {
                    // Multiple entries - let user choose
                    console.log('[CipherMesh] Multiple entries found, showing selector');
                    await showCredentialSelector(entries, usernameField, passwordField);
                }
            } else {
                console.error('[CipherMesh] Failed to get credentials:', response);
                await showAlertDialog('Failed to get credentials: ' + (response.error || 'Unknown error'), 'Error', true);
            }
        } catch (error) {
            console.error('[CipherMesh] Error getting credentials:', error);
            await showAlertDialog('Error: ' + error.message, 'Error', true);
        }
    }
    
    // Fill credentials into form
    function fillCredentials(entry, usernameField, passwordField) {
        if (usernameField) {
            usernameField.value = entry.username;
            usernameField.dispatchEvent(new Event('input', { bubbles: true }));
            usernameField.dispatchEvent(new Event('change', { bubbles: true }));
        }
        
        // Mark as autofilled BEFORE setting value to prevent triggering save prompt
        passwordField.dataset.ciphermeshAutofilled = 'true';
        
        passwordField.value = entry.password;
        passwordField.dispatchEvent(new Event('input', { bubbles: true }));
        passwordField.dispatchEvent(new Event('change', { bubbles: true }));
    }
    
    // Show credential selector when multiple matches
    function showCredentialSelector(entries, usernameField, passwordField) {
        const selector = document.createElement('div');
        selector.style.cssText = `
            position: fixed;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            background: white;
            border: 2px solid #569cd6;
            border-radius: 8px;
            padding: 20px;
            box-shadow: 0 4px 20px rgba(0,0,0,0.3);
            z-index: 100000;
            max-width: 400px;
        `;
        
        selector.innerHTML = `
            <h3 style="margin: 0 0 15px 0; color: #333;">Select Account</h3>
            <div id="ciphermesh-entries"></div>
            <button id="ciphermesh-cancel" style="margin-top: 15px; padding: 8px 16px; background: #ccc; border: none; border-radius: 4px; cursor: pointer;">Cancel</button>
        `;
        
        const entriesDiv = selector.querySelector('#ciphermesh-entries');
        entries.forEach(entry => {
            const btn = document.createElement('button');
            btn.textContent = entry.username || entry.title;
            btn.style.cssText = `
                display: block;
                width: 100%;
                padding: 10px;
                margin-bottom: 8px;
                background: #569cd6;
                color: white;
                border: none;
                border-radius: 4px;
                cursor: pointer;
                text-align: left;
            `;
            btn.addEventListener('click', () => {
                fillCredentials(entry, usernameField, passwordField);
                document.body.removeChild(selector);
            });
            entriesDiv.appendChild(btn);
        });
        
        selector.querySelector('#ciphermesh-cancel').addEventListener('click', () => {
            document.body.removeChild(selector);
        });
        
        document.body.appendChild(selector);
    }
    
    // Show the save password prompt dialog (for non-form submissions)
    async function showSavePasswordPrompt(url, username, password) {
        console.log('[CipherMesh] Showing save password prompt (no form)');
        
        try {
            const shouldSave = await showConfirmDialog(
                `Save password for <strong>${username}</strong> on <strong>${url}</strong> to your CipherMesh vault?`,
                'Save Password'
            );
            
            if (shouldSave) {
                console.log('[CipherMesh] User chose to save password');
                promptSaveCredentials(url, username, password);
            } else {
                console.log('[CipherMesh] User declined to save password');
            }
        } catch (error) {
            console.error('[CipherMesh] Error showing save prompt:', error);
        }
    }
    
    // Show group selector dialog
    function showGroupSelector(groups) {
        return new Promise((resolve) => {
            const content = document.createElement('div');
            content.innerHTML = `
                <p style="margin: 0 0 16px 0; color: #666; font-size: 14px;">
                    Choose which group to save this password to
                </p>
                <div id="ciphermesh-group-list" style="
                    max-height: 300px;
                    overflow-y: auto;
                "></div>
            `;
            
            const groupList = content.querySelector('#ciphermesh-group-list');
            
            groups.forEach((group, index) => {
                const groupBtn = document.createElement('button');
                groupBtn.textContent = group;
                groupBtn.style.cssText = `
                    display: block;
                    width: 100%;
                    padding: 12px 16px;
                    margin-bottom: 8px;
                    background: white;
                    color: #333;
                    border: 2px solid ${index === 0 ? '#667eea' : '#e0e0e0'};
                    border-radius: 8px;
                    font-size: 14px;
                    font-weight: 500;
                    cursor: pointer;
                    text-align: left;
                    transition: all 0.2s;
                `;
                
                groupBtn.addEventListener('mouseenter', () => {
                    groupBtn.style.borderColor = '#667eea';
                    groupBtn.style.background = '#f8f9ff';
                });
                groupBtn.addEventListener('mouseleave', () => {
                    groupBtn.style.borderColor = index === 0 ? '#667eea' : '#e0e0e0';
                    groupBtn.style.background = 'white';
                });
                groupBtn.addEventListener('click', () => {
                    document.body.removeChild(modal);
                    resolve(group);
                });
                
                groupList.appendChild(groupBtn);
            });
            
            const cancelButton = document.createElement('button');
            cancelButton.textContent = 'Cancel';
            cancelButton.style.cssText = `
                padding: 10px 24px;
                background: white;
                color: #666;
                border: 2px solid #e0e0e0;
                border-radius: 6px;
                font-size: 14px;
                font-weight: 600;
                cursor: pointer;
                transition: all 0.2s;
            `;
            cancelButton.addEventListener('mouseenter', () => {
                cancelButton.style.borderColor = '#999';
                cancelButton.style.color = '#333';
            });
            cancelButton.addEventListener('mouseleave', () => {
                cancelButton.style.borderColor = '#e0e0e0';
                cancelButton.style.color = '#666';
            });
            
            const modal = createModal('Select Group', content, [cancelButton]);
            
            cancelButton.addEventListener('click', () => {
                document.body.removeChild(modal);
                resolve(null);
            });
            
            document.body.appendChild(modal);
        });
    }
    
    // Prompt to save credentials
    async function promptSaveCredentials(url, username, password) {
        const masterPassword = await showMasterPasswordDialog();
        if (!masterPassword) return;
        
        // Verify master password
        try {
            const verifyResponse = await chrome.runtime.sendMessage({
                type: "VERIFY_MASTER_PASSWORD",
                password: masterPassword
            });
            
            if (!verifyResponse.success || !verifyResponse.verified) {
                await showAlertDialog('The master password you entered is incorrect.', 'Incorrect Password', true);
                return;
            }
        } catch (error) {
            await showAlertDialog('Failed to verify password: ' + error.message, 'Error', true);
            return;
        }
        
        // Get available groups
        try {
            const groupsResponse = await chrome.runtime.sendMessage({
                type: "LIST_GROUPS"
            });
            
            if (groupsResponse.success && groupsResponse.groups) {
                const groups = groupsResponse.groups;
                let groupName = groups[0] || 'Default';
                
                if (groups.length > 1) {
                    // Show group selector
                    const selected = await showGroupSelector(groups);
                    if (!selected) return;
                    groupName = selected;
                }
                
                // Save credentials
                const saveResponse = await chrome.runtime.sendMessage({
                    type: "SAVE_CREDENTIALS",
                    url: url,
                    username: username,
                    password: password,
                    title: document.title || url,
                    group: groupName
                });
                
                if (saveResponse.success) {
                    await showAlertDialog(`Password for <strong>${username}</strong> has been saved to group <strong>${groupName}</strong>!`, 'Password Saved');
                } else {
                    await showAlertDialog('Failed to save: ' + (saveResponse.error || 'Unknown error'), 'Error', true);
                }
            }
        } catch (error) {
            await showAlertDialog('Error: ' + error.message, 'Error', true);
        }
    }
    
    // Scan on page load
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', scanForPasswordFields);
    } else {
        scanForPasswordFields();
    }
    
    // Watch for dynamically added password fields
    const observer = new MutationObserver(() => {
        if (isConnected) {
            scanForPasswordFields();
        }
    });
    
    observer.observe(document.body, {
        childList: true,
        subtree: true
    });
    
})();
