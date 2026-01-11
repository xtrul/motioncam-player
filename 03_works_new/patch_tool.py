# Enhanced PatchApplierGUI with improvements for usability, robustness, and structure
import os
import shutil
import tkinter as tk
from tkinter import Tk, filedialog, messagebox, ttk, Menu, Toplevel, Label, Button, StringVar
from tkinter.scrolledtext import ScrolledText
# Ensure unidiff is installed: pip install unidiff
try:
    from unidiff import PatchSet, PatchedFile, Hunk, UnidiffParseError
except ImportError:
    print("Error: 'unidiff' library not found.")
    print("Please install it using: pip install unidiff")
    exit()
from enum import Enum
import datetime
from threading import Thread
import traceback # For better error reporting

class PatchFileStatus(Enum):
    PENDING = 'Pending'
    APPLIED = 'Applied'
    FAILED = 'Failed'
    RESTORED = 'Restored'
    VALID = 'Valid (Dry Run)'
    MISMATCH = 'Mismatch (Dry Run)'
    MISSING = 'Missing File'
    NO_CHANGES = 'No Changes' # Added for clarity

class PatchApplierGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Visual Patch Applier")
        self.patch_set: PatchSet | None = None
        self.source_folder: str | None = None
        self.raw_patch_text: str = ""
        self.file_status: dict[str, PatchFileStatus] = {} # Maps patched_file.path to status
        self.source_label_var = StringVar(value="No source folder selected")
        self.build_gui()

    def build_gui(self):
        # --- Menu ---
        self.menu = Menu(self.root)
        self.root.config(menu=self.menu)
        help_menu = Menu(self.menu, tearoff=0)
        help_menu.add_command(label="Patch Format Help", command=self.show_help)
        help_menu.add_command(label="Show ChatGPT Prompt", command=self.show_prompt)
        help_menu.add_command(label="View Raw Patch Content", command=self.show_raw_patch)
        self.menu.add_cascade(label="Help", menu=help_menu)

        # --- Main Frame ---
        self.frame = ttk.Frame(self.root, padding=10)
        self.frame.pack(fill='both', expand=True)

        # --- Top Row: Buttons & Label ---
        top_frame = ttk.Frame(self.frame)
        top_frame.grid(row=0, column=0, columnspan=3, sticky='ew', pady=(0, 10))

        self.button_load_patch = ttk.Button(top_frame, text="Load Patch File", command=self.load_patch)
        self.button_load_patch.pack(side='left', padx=(0, 5))

        self.button_select_folder = ttk.Button(top_frame, text="Select Source Folder", command=self.select_source_folder)
        self.button_select_folder.pack(side='left', padx=(0, 5))

        self.source_label = ttk.Label(top_frame, textvariable=self.source_label_var, anchor='w')
        self.source_label.pack(side='left', fill='x', expand=True, padx=(5, 0))

        # --- Middle Row: Treeview ---
        self.tree = ttk.Treeview(self.frame, columns=("file", "status", "summary"), show='headings')
        self.tree.heading("file", text="Filename")
        self.tree.heading("status", text="Status")
        self.tree.heading("summary", text="Summary")
        self.tree.column("file", width=250, stretch=tk.YES)
        self.tree.column("status", width=120, stretch=tk.NO, anchor='center') # Centered status
        self.tree.column("summary", width=150, stretch=tk.NO)
        self.tree.grid(row=1, column=0, columnspan=3, sticky='nsew')
        self.tree.bind("<<TreeviewSelect>>", self.preview_selected_patch)

        # Add Scrollbar to Treeview
        tree_scrollbar = ttk.Scrollbar(self.frame, orient="vertical", command=self.tree.yview)
        tree_scrollbar.grid(row=1, column=3, sticky='ns')
        self.tree.configure(yscrollcommand=tree_scrollbar.set)

        self.frame.rowconfigure(1, weight=1) # Allow treeview to expand vertically
        self.frame.columnconfigure(0, weight=1) # Allow columns to adjust (adjust weights as needed)
        # Give less weight to columns 1 and 2 if desired
        self.frame.columnconfigure(1, weight=0)
        self.frame.columnconfigure(2, weight=0)


        # --- Preview Panes ---
        preview_frame = ttk.Frame(self.frame)
        preview_frame.grid(row=2, column=0, columnspan=3, sticky='nsew', pady=(10, 5))
        preview_frame.columnconfigure(0, weight=1)
        preview_frame.columnconfigure(1, weight=1)
        preview_frame.rowconfigure(1, weight=1) # Allow text widgets to expand

        ttk.Label(preview_frame, text="Patch Preview / Original (from file)").grid(row=0, column=0, sticky='w')
        ttk.Label(preview_frame, text="Applied Preview / Target").grid(row=0, column=1, sticky='w')

        self.text_original = ScrolledText(preview_frame, width=60, height=15, wrap='none') # Use wrap='none' for code
        self.text_patched = ScrolledText(preview_frame, width=60, height=15, wrap='none')
        self.text_original.grid(row=1, column=0, sticky='nsew', padx=(0, 5))
        self.text_patched.grid(row=1, column=1, sticky='nsew', padx=(5, 0))


        # --- Bottom Row: Action Buttons ---
        button_frame = ttk.Frame(self.frame)
        button_frame.grid(row=3, column=0, columnspan=3, sticky='ew', pady=(5, 0))

        self.button_validate = ttk.Button(button_frame, text="Validate Selected", command=self.validate_selected_patch_threaded)
        self.button_validate.pack(side='left', padx=(0, 5))

        self.button_apply = ttk.Button(button_frame, text="Apply Selected", command=self.apply_selected_patch_threaded)
        self.button_apply.pack(side='left', padx=(0, 5))

        self.button_restore = ttk.Button(button_frame, text="Restore Selected from Backup", command=self.restore_backup_threaded)
        self.button_restore.pack(side='left', padx=(0, 5))

        self.button_export_log = ttk.Button(button_frame, text="Export Patch Log", command=self.export_log)
        self.button_export_log.pack(side='right', padx=(5, 0)) # Align right


    def show_help(self):
        messagebox.showinfo("Patch Format Help",
                            "This tool applies patches in the unified diff format (.patch, .diff).\n\n"
                            "1. Load a patch file.\n"
                            "2. Select the root source code folder.\n"
                            "3. Select file(s) in the list.\n"
                            "4. (Optional) Validate: Checks if the patch *can* be applied without changing files. Looks for context mismatches.\n"
                            "5. Apply: Attempts to apply the changes. Creates backups (*.timestamp.bak).\n"
                            "6. Restore: Reverts the selected file(s) using the latest backup.\n\n"
                            "Patch application requires the source file context to *exactly* match the patch, including line endings and whitespace unless handled specifically.")

    def load_patch(self):
        file_path = filedialog.askopenfilename(filetypes=[("Patch files", "*.patch *.diff"), ("All files", "*.*")])
        if not file_path:
            return
        try:
            # Read raw text first for later display
            # Use errors='replace' to avoid crashing on potential encoding issues
            # Try common encodings if utf-8 fails
            encodings_to_try = ['utf-8', 'latin-1', 'cp1252']
            patch_text = None
            detected_encoding = None
            for enc in encodings_to_try:
                try:
                    with open(file_path, 'r', encoding=enc) as f_check:
                        patch_text = f_check.read()
                    detected_encoding = enc
                    print(f"Successfully read patch file with encoding: {detected_encoding}")
                    break # Stop trying once successful
                except UnicodeDecodeError:
                    continue # Try next encoding
                except Exception as read_err: # Catch other potential read errors
                     messagebox.showerror("Error Reading Patch", f"Failed to read patch file:\n{file_path}\n\nEncoding: {enc}\nError: {read_err}")
                     return # Stop if a non-decode error occurs

            if patch_text is None:
                messagebox.showerror("Encoding Error", f"Could not decode the patch file using common encodings ({', '.join(encodings_to_try)}).\nPlease ensure it's saved as UTF-8 or a compatible format.")
                return

            self.raw_patch_text = patch_text

            # *** FIX HERE ***
            # Pass the already decoded string directly. Do NOT specify encoding here,
            # as the string is already decoded using detected_encoding.
            self.raw_patch_text = self.raw_patch_text.replace('\r\n', '\n').replace('\r', '\n')
            self.patch_set = PatchSet(self.raw_patch_text)

            # Basic check if parsing yielded anything
            if not self.patch_set.modified_files and not self.patch_set.added_files and not self.patch_set.removed_files:
                 # Check if it's just headers/comments
                 if len(self.raw_patch_text.strip()) > 0 and not any(line.startswith(('---', '+++', '@@')) for line in self.raw_patch_text.splitlines()):
                     messagebox.showwarning("Parsing Issue", f"File '{os.path.basename(file_path)}' loaded, but it doesn't appear to contain standard patch format headers (---, +++, @@).")
                 elif not self.raw_patch_text.strip():
                     messagebox.showinfo("Empty File", f"The loaded file '{os.path.basename(file_path)}' is empty.")
                 else:
                     # It might be a valid patch with no changes if it just contains headers
                     pass # Allow loading, tree population will handle it
                 # Don't clear raw_patch_text here, user might want to view it

        except UnidiffParseError as pe:
             messagebox.showerror("Patch Parsing Error", f"Failed to parse patch file (check format and hunk counts):\n{file_path}\n\nError: {pe}\n\n{traceback.format_exc()}")
             self.patch_set = None
             self.raw_patch_text = "" # Clear raw text if parsing failed badly
             return
        except Exception as e:
            # Catch potential errors from the PatchSet() call itself now
            if "decoding str is not supported" in str(e): # More specific check if needed
                 messagebox.showerror("Internal Error", f"Error passing decoded string to PatchSet:\n{e}\n\nPlease report this bug.")
            else:
                 messagebox.showerror("Error Loading Patch", f"An unexpected error occurred loading or parsing:\n{file_path}\n\nError: {e}\n\n{traceback.format_exc()}")
            self.patch_set = None
            self.raw_patch_text = ""
            return

        # --- Populate Treeview ---
        # (Rest of the function remains the same)
        self.tree.delete(*self.tree.get_children())
        self.file_status.clear()
        self.text_original.delete(1.0, 'end')
        self.text_patched.delete(1.0, 'end')

        if not self.patch_set or (not self.patch_set.modified_files and not self.patch_set.added_files and not self.patch_set.removed_files):
             if not (len(self.raw_patch_text.strip()) > 0 and not any(line.startswith(('---', '+++', '@@')) for line in self.raw_patch_text.splitlines())) and self.raw_patch_text.strip():
                 messagebox.showinfo("Empty Patch", "The loaded patch file was parsed but contains no file changes.")
             if self.patch_set is None:
                 return

        all_patched_files = self.patch_set

        for patched_file in all_patched_files:
            file_id = patched_file.path

            if len(patched_file) == 0 and not patched_file.is_added_file and not patched_file.is_removed_file:
                 summary = "No changes in patch"
                 status = PatchFileStatus.NO_CHANGES
            elif patched_file.is_added_file:
                 summary = f"+{patched_file.added} lines (New File)"
                 status = PatchFileStatus.PENDING
            elif patched_file.is_removed_file:
                 summary = f"-{patched_file.removed} lines (Removed File)"
                 status = PatchFileStatus.PENDING
            else: # Modified file
                summary = f"+{patched_file.added} -{patched_file.removed} ({len(patched_file)} hunks)"
                status = PatchFileStatus.PENDING

            self.tree.insert('', 'end', iid=file_id, values=(file_id, status.value, summary))
            self.file_status[file_id] = status
        file_path = filedialog.askopenfilename(filetypes=[("Patch files", "*.patch *.diff"), ("All files", "*.*")])
        if not file_path:
            return
        try:
            # Read raw text first for later display
            # Use errors='replace' to avoid crashing on potential encoding issues
            # Try common encodings if utf-8 fails
            encodings_to_try = ['utf-8', 'latin-1', 'cp1252']
            patch_text = None
            detected_encoding = None
            for enc in encodings_to_try:
                try:
                    with open(file_path, 'r', encoding=enc) as f_check:
                        patch_text = f_check.read()
                    detected_encoding = enc
                    print(f"Successfully read patch file with encoding: {detected_encoding}")
                    break # Stop trying once successful
                except UnicodeDecodeError:
                    continue # Try next encoding
                except Exception as read_err: # Catch other potential read errors
                     messagebox.showerror("Error Reading Patch", f"Failed to read patch file:\n{file_path}\n\nEncoding: {enc}\nError: {read_err}")
                     return # Stop if a non-decode error occurs

            if patch_text is None:
                messagebox.showerror("Encoding Error", f"Could not decode the patch file using common encodings ({', '.join(encodings_to_try)}).\nPlease ensure it's saved as UTF-8 or a compatible format.")
                return

            self.raw_patch_text = patch_text

            # Parse using the detected encoding
            self.patch_set = PatchSet(self.raw_patch_text)

            # Basic check if parsing yielded anything
            if not self.patch_set.modified_files and not self.patch_set.added_files and not self.patch_set.removed_files:
                 # Check if it's just headers/comments
                 if len(self.raw_patch_text.strip()) > 0 and not any(line.startswith(('---', '+++', '@@')) for line in self.raw_patch_text.splitlines()):
                     messagebox.showwarning("Parsing Issue", f"File '{os.path.basename(file_path)}' loaded, but it doesn't appear to contain standard patch format headers (---, +++, @@).")
                 elif not self.raw_patch_text.strip():
                     messagebox.showinfo("Empty File", f"The loaded file '{os.path.basename(file_path)}' is empty.")
                 else:
                     # It might be a valid patch with no changes if it just contains headers
                     pass # Allow loading, tree population will handle it
                 # Don't clear raw_patch_text here, user might want to view it

        except UnidiffParseError as pe:
             messagebox.showerror("Patch Parsing Error", f"Failed to parse patch file (check format and hunk counts):\n{file_path}\n\nError: {pe}\n\n{traceback.format_exc()}")
             self.patch_set = None
             self.raw_patch_text = "" # Clear raw text if parsing failed badly
             return
        except Exception as e:
            messagebox.showerror("Error Loading Patch", f"An unexpected error occurred loading or parsing:\n{file_path}\n\nError: {e}\n\n{traceback.format_exc()}")
            self.patch_set = None
            self.raw_patch_text = ""
            return

        # --- Populate Treeview ---
        self.tree.delete(*self.tree.get_children())
        self.file_status.clear()
        self.text_original.delete(1.0, 'end')
        self.text_patched.delete(1.0, 'end')

        # Check again after parsing, as PatchSet might be created but empty
        if not self.patch_set or (not self.patch_set.modified_files and not self.patch_set.added_files and not self.patch_set.removed_files):
             # Only show this if the file wasn't just headers/comments or empty
             if not (len(self.raw_patch_text.strip()) > 0 and not any(line.startswith(('---', '+++', '@@')) for line in self.raw_patch_text.splitlines())) and self.raw_patch_text.strip():
                 messagebox.showinfo("Empty Patch", "The loaded patch file was parsed but contains no file changes.")
             # Keep the view populated if it was just headers
             if self.patch_set is None: # If parsing truly failed to create PatchSet
                 return


        # Process all files mentioned in the patch
        all_patched_files = self.patch_set # This includes modified, added, removed

        for patched_file in all_patched_files:
            # Use patched_file.path as the unique identifier
            file_id = patched_file.path # e.g., 'src/App.cpp' or 'include/Renderer.h'

            if len(patched_file) == 0 and not patched_file.is_added_file and not patched_file.is_removed_file:
                 summary = "No changes in patch"
                 status = PatchFileStatus.NO_CHANGES
            elif patched_file.is_added_file:
                 summary = f"+{patched_file.added} lines (New File)"
                 status = PatchFileStatus.PENDING
            elif patched_file.is_removed_file:
                 summary = f"-{patched_file.removed} lines (Removed File)"
                 status = PatchFileStatus.PENDING
            else: # Modified file
                summary = f"+{patched_file.added} -{patched_file.removed} ({len(patched_file)} hunks)"
                status = PatchFileStatus.PENDING

            # Use file_id (the relative path) as the display name too
            self.tree.insert('', 'end', iid=file_id, values=(file_id, status.value, summary))
            self.file_status[file_id] = status

    def select_source_folder(self):
        folder = filedialog.askdirectory(mustexist=True, title="Select Root Source Code Folder")
        if folder:
            self.source_folder = os.path.normpath(folder)
            self.source_label_var.set(f"Source: {self.source_folder}")
            # Reset status if folder changes, as validation depends on it
            self._reset_tree_status()

    def _reset_tree_status(self):
         """Resets the status column in the treeview to Pending."""
         for item_id in self.tree.get_children():
             current_values = list(self.tree.item(item_id, 'values'))
             if len(current_values) >= 2:
                 # Only reset if not already 'No Changes' or similar fixed status
                 if self.file_status.get(item_id) not in (PatchFileStatus.NO_CHANGES, ):
                     current_values[1] = PatchFileStatus.PENDING.value
                     self.tree.item(item_id, values=tuple(current_values))
                     self.file_status[item_id] = PatchFileStatus.PENDING
         self.text_original.delete(1.0, 'end')
         self.text_patched.delete(1.0, 'end')


    def _run_threaded(self, target_func, button, busy_text="Working...", finished_text=None):
        """Helper to run a function in a thread and manage button state."""
        original_text = button['text']
        if not finished_text:
            finished_text = original_text

        def run():
            self.root.after(0, lambda: button.config(state='disabled', text=busy_text))
            try:
                target_func()
            except Exception as e:
                 # Show errors that occur within the thread
                 error_message = f"An error occurred in the background task:\n{e}\n\n{traceback.format_exc()}"
                 print(error_message) # Log to console as well
                 self.root.after(0, lambda: messagebox.showerror("Thread Error", error_message))
            finally:
                # Ensure the button is re-enabled even if the window is closing
                if self.root.winfo_exists():
                    self.root.after(0, lambda: button.config(state='normal', text=finished_text))

        thread = Thread(target=run, daemon=True) # Use daemon thread
        thread.start()

    def apply_selected_patch_threaded(self):
        if not self._check_prerequisites(): return
        selected_ids = self.tree.selection()
        if not selected_ids:
            messagebox.showwarning("No Selection", "Please select one or more files from the list to apply.")
            return
        self._run_threaded(lambda: self._apply_or_validate_patches(selected_ids, dry_run=False),
                           self.button_apply, "Applying...")

    def validate_selected_patch_threaded(self):
        if not self._check_prerequisites(): return
        selected_ids = self.tree.selection()
        if not selected_ids:
            # If nothing selected, validate all pending/mismatched files
            selected_ids = [item_id for item_id in self.tree.get_children()
                            if self.file_status.get(item_id) in (PatchFileStatus.PENDING, PatchFileStatus.MISMATCH, PatchFileStatus.FAILED)]
            if not selected_ids:
                 messagebox.showinfo("Validate", "No pending or previously failed files to validate.")
                 return
            action = "all pending/failed"
        else:
            action = f"{len(selected_ids)} selected"

        print(f"--- Starting Validation ({action} files) ---")
        self._run_threaded(lambda: self._apply_or_validate_patches(selected_ids, dry_run=True),
                           self.button_validate, "Validating...")

    def restore_backup_threaded(self):
        if not self.source_folder:
            messagebox.showwarning("No Folder", "Please select the source folder first.")
            return
        selected_ids = self.tree.selection()
        if not selected_ids:
            messagebox.showwarning("No Selection", "Please select one or more files to restore.")
            return
        self._run_threaded(lambda: self._restore_selected(selected_ids),
                            self.button_restore, "Restoring...")

    def _check_prerequisites(self):
        """Checks if patch and source folder are set."""
        if self.patch_set is None:
            messagebox.showwarning("No Patch", "Please load a patch file first.")
            return False
        if not self.source_folder:
            messagebox.showwarning("No Folder", "Please select the source folder first.")
            return False
        return True

    def _find_patched_file(self, file_id: str) -> PatchedFile | None:
        """Finds the PatchedFile object corresponding to the tree item ID."""
        if self.patch_set is None: return None
        # Since file_id IS patched_file.path, we can search directly
        return next((pf for pf in self.patch_set if pf.path == file_id), None)

    def _apply_or_validate_patches(self, file_ids: list[str], dry_run: bool):
        """Core logic for both applying and validating patches."""
        action = "Validation" if dry_run else "Application"
        action_verb = "validate" if dry_run else "apply"
        results = {"success": 0, "fail": 0, "missing": 0, "skipped": 0, "no_change": 0}

        processed_count = 0
        total_to_process = len(file_ids)

        for file_id in file_ids:
            processed_count += 1
            print(f"--- {action} ({processed_count}/{total_to_process}): Processing '{file_id}' ---")

            patched_file = self._find_patched_file(file_id)
            if not patched_file:
                print(f"Warning: Could not find patch data for tree item '{file_id}' - Skipping.")
                self._update_status(file_id, PatchFileStatus.FAILED, "Internal error: Patch data not found.")
                results["skipped"] += 1
                continue # Should not happen if tree is populated correctly

            # Skip files marked as having no changes in the patch
            if len(patched_file) == 0 and not patched_file.is_added_file and not patched_file.is_removed_file:
                 print(f"Skipping '{file_id}': Patch specifies no changes for this file.")
                 self._update_status(file_id, PatchFileStatus.NO_CHANGES)
                 results["no_change"] += 1
                 continue

            # Construct full path using the patch's relative path
            # Patches usually use '/', os.path.join handles platform differences
            try:
                full_path = os.path.join(self.source_folder, *file_id.split('/'))
                full_path = os.path.normpath(full_path) # Normalize separators (\ vs /)
            except Exception as path_e:
                print(f"Error constructing path for '{file_id}': {path_e} - Skipping.")
                self._update_status(file_id, PatchFileStatus.FAILED, f"Path construction error: {path_e}")
                results["fail"] += 1
                continue

            print(f"Source file path: {full_path}")

            # --- Handle File Existence ---
            try:
                file_exists = os.path.exists(full_path)
                is_dir = os.path.isdir(full_path)
            except OSError as os_err:
                 print(f"Error checking existence of '{full_path}': {os_err} - Skipping.")
                 self._update_status(file_id, PatchFileStatus.FAILED, f"OS error checking file: {os_err}")
                 results["fail"] += 1
                 continue

            if is_dir:
                print(f"Skipping '{file_id}': Path points to a directory, not a file.")
                self._update_status(file_id, PatchFileStatus.FAILED, "Path is a directory.")
                results["fail"] += 1
                continue

            # --- Handle Added File ---
            if patched_file.is_added_file:
                print(f"Patch type: Added File")
                if file_exists:
                     status = PatchFileStatus.MISMATCH if dry_run else PatchFileStatus.FAILED
                     msg = f"Cannot {action_verb} added file patch: '{file_id}' already exists."
                     self._update_status(file_id, status, msg)
                     results["fail"] += 1
                     print(msg)
                     continue
                elif not dry_run:
                     # Apply logic for adding file
                     print("Action: Creating new file.")
                     try:
                         os.makedirs(os.path.dirname(full_path), exist_ok=True)
                         # Use newline='' to prevent Python adding extra \r on Windows
                         # Write lines marked as added from the patch
                         # Ensure lines have appropriate endings (unidiff usually preserves them)
                         with open(full_path, 'w', encoding='utf-8', newline='') as f:
                             f.writelines(line.value for hunk in patched_file for line in hunk if line.is_added)
                         self._update_status(file_id, PatchFileStatus.APPLIED)
                         results["success"] += 1
                         print("Successfully created.")
                     except Exception as e:
                         msg = f"Failed to create new file {file_id}: {e}"
                         self._update_status(file_id, PatchFileStatus.FAILED, msg)
                         results["fail"] += 1
                         print(f"Error: {msg}\n{traceback.format_exc()}")
                     continue
                else: # Dry run for added file
                    msg = "File would be created."
                    self._update_status(file_id, PatchFileStatus.VALID, msg)
                    results["success"] += 1
                    print(msg)
                    continue # Skip further processing for added files

            # --- Handle Removed File ---
            elif patched_file.is_removed_file:
                print(f"Patch type: Removed File")
                if not file_exists:
                     # File is already gone or never existed
                     status = PatchFileStatus.VALID if dry_run else PatchFileStatus.APPLIED # Consider it applied if gone
                     msg = "File to be removed does not exist (already gone?)."
                     self._update_status(file_id, status, msg)
                     # This might be okay or an error depending on context, treat as success/valid for now
                     results["success"] += 1
                     print(msg)
                     continue
                elif not dry_run:
                    # Apply logic for removing file (backup first!)
                    print("Action: Removing file.")
                    backup_path = self._create_backup(full_path)
                    if backup_path:
                        try:
                             os.remove(full_path)
                             self._update_status(file_id, PatchFileStatus.APPLIED, f"File removed. Backup: {os.path.basename(backup_path)}")
                             results["success"] += 1
                             print("Successfully removed.")
                        except Exception as e:
                             msg = f"Failed to remove file {file_id} after backup: {e}"
                             self._update_status(file_id, PatchFileStatus.FAILED, msg)
                             results["fail"] += 1
                             print(f"Error: {msg}\n{traceback.format_exc()}")
                    else: # Backup failed
                         # Status already updated in _create_backup
                         results["fail"] += 1
                         print("Skipping removal due to backup failure.")
                    continue
                else: # Dry run for removed file
                    msg = "File would be removed."
                    self._update_status(file_id, PatchFileStatus.VALID, msg)
                    results["success"] += 1
                    print(msg)
                    continue # Skip further processing for removed files

            # --- Handle Modified File ---
            elif not file_exists:
                # Trying to modify a file that doesn't exist
                msg = f"Source file '{file_id}' not found."
                self._update_status(file_id, PatchFileStatus.MISSING, msg)
                results["missing"] += 1
                print(msg)
                continue

            # --- File exists and patch is for modification ---
            print(f"Patch type: Modified File")
            try:
                # Read source file content, trying multiple encodings
                original_lines = None
                detected_encoding = None
                encodings_to_try = ['utf-8', 'latin-1', 'cp1252'] # Same as patch loading
                for enc in encodings_to_try:
                     try:
                         with open(full_path, 'r', encoding=enc) as f:
                             original_lines = f.readlines() # Keep original line endings
                         detected_encoding = enc
                         print(f"Successfully read source file with encoding: {detected_encoding}")
                         break
                     except UnicodeDecodeError:
                         continue
                     except Exception as read_err:
                         raise IOError(f"Failed reading source file '{file_id}' with encoding {enc}: {read_err}") from read_err

                if original_lines is None:
                     raise ValueError(f"Could not decode source file '{file_id}' using common encodings.")

                # --- Pre-validation Step ---
                print("Validating hunks against source file...")
                validation_result, validation_message = self._validate_hunks(patched_file, original_lines)

                if not validation_result:
                    status = PatchFileStatus.MISMATCH # Keep as mismatch even if applying
                    self._update_status(file_id, status, validation_message)
                    results["fail"] += 1
                    print(f"Validation failed: {validation_message}")
                    continue # Stop processing this file if validation fails

                # --- If validating, we're done for this file ---
                if dry_run:
                    msg = "Patch context matches source."
                    self._update_status(file_id, PatchFileStatus.VALID, msg)
                    results["success"] += 1
                    print("Validation successful.")
                    continue

                # --- Apply the patch (only if not dry_run and validation passed) ---
                print("Action: Applying validated patch.")
                backup_path = self._create_backup(full_path)
                if not backup_path:
                    # Status already updated in _create_backup
                    results["fail"] += 1
                    print("Skipping application due to backup failure.")
                    continue # Skip applying if backup failed

                # Apply changes (more robust approach needed for complex cases,
                # but this works if validation is strict)
                patched_lines = original_lines[:] # Create a copy
                offset = 0 # Keep track of line number changes due to additions/deletions

                for hunk_index, hunk in enumerate(patched_file):
                    # We already validated, so apply directly based on hunk info
                    # unidiff lines are 1-based, list indices are 0-based
                    start_index = hunk.source_start - 1 + offset
                    print(f"  Applying Hunk #{hunk_index + 1}: Deleting {hunk.source_length} lines from index {start_index}, Inserting {hunk.target_length} lines.")

                    # Remove original lines specified by the hunk
                    if hunk.source_length > 0:
                         del patched_lines[start_index : start_index + hunk.source_length]

                    # Insert new lines from the hunk
                insert_pos = start_index
                hunk_target_lines = hunk.target_lines() # Get target lines from unidiff
                for line_value in hunk_target_lines:
                    # Ensure line value has appropriate ending if needed?
                    # unidiff usually preserves endings from patch file
                    # Use .value to get the string from the Line object
                    patched_lines.insert(insert_pos, line_value.value) # <--- FIXED LINE
                    insert_pos += 1

                    # Update offset for the next hunk
                    offset += (hunk.target_length - hunk.source_length)


                # Write the patched content back to the file using detected encoding
                # Use newline='' to prevent Python adding extra \r on Windows
                print(f"Writing {len(patched_lines)} patched lines back to file using encoding {detected_encoding}.")
                with open(full_path, 'w', encoding=detected_encoding, newline='') as f:
                    f.writelines(patched_lines)

                self._update_status(file_id, PatchFileStatus.APPLIED, f"Patch applied. Backup: {os.path.basename(backup_path)}")
                results["success"] += 1
                print("Successfully applied.")

                # Optionally update preview after successful application
                # Combine lines back into single strings for preview
                original_content_str = "".join(original_lines)
                patched_content_str = "".join(patched_lines)
                self.root.after(0, lambda fid=file_id, orig=original_content_str, patch=patched_content_str: self._update_preview_content(fid, orig, patch))


            except Exception as e:
                error_msg = f"Error processing file {file_id}: {e}\n{traceback.format_exc()}"
                print(f"Error: {error_msg}") # Log full error
                status = PatchFileStatus.FAILED # Mark as Failed on error
                self._update_status(file_id, status, f"Error: {e}")
                results["fail"] += 1

        # --- Final Summary Message ---
        total_processed = sum(results.values()) - results["skipped"] # Count files where an action was attempted
        message = f"{action} Complete for {total_processed} file(s) ({total_to_process} selected):\n\n"
        if results["success"] > 0: message += f"- Succeeded / Valid: {results['success']}\n"
        if results["fail"] > 0: message += f"- Failed / Mismatch: {results['fail']}\n"
        if results["missing"] > 0: message += f"- Source File Missing: {results['missing']}\n"
        if results["no_change"] > 0: message += f"- No Changes in Patch: {results['no_change']}\n"
        if results["skipped"] > 0: message += f"- Skipped (Internal Issue): {results['skipped']}\n"

        # Ensure the final message box appears on the main thread
        self.root.after(0, lambda: messagebox.showinfo(f"{action} Results", message))
        print(f"--- {action} Finished ---")


    def _validate_hunks(self, patched_file: PatchedFile, source_lines: list[str]) -> tuple[bool, str]:
        """
        Checks if all hunks in a PatchedFile match the source lines.
        Returns (True, "") if valid, or (False, "Error message") if not.
        """
        for hunk_index, hunk in enumerate(patched_file):
            # Calculate expected start line in the *original* source_lines list
            # Hunk source_start is 1-based
            expected_start_index = hunk.source_start - 1

            # Basic bounds check
            if expected_start_index < 0:
                 return False, f"Hunk #{hunk_index + 1} has invalid start line {hunk.source_start}."

            # Check if the hunk goes beyond the file length
            # Note: A hunk can validly *touch* the end of the file (index == len(lines))
            # if it's adding lines there, but its source lines must exist.
            if (expected_start_index + hunk.source_length) > len(source_lines):
                 return False, (f"Hunk #{hunk_index + 1} (starts line {hunk.source_start}, length {hunk.source_length}) "
                                f"extends beyond end of file ({len(source_lines)} lines).")

            # Extract the slice of lines from the source file that this hunk should match
            source_hunk_lines = source_lines[expected_start_index : expected_start_index + hunk.source_length]

            # Get the lines from the patch hunk that represent the *original* source
            # (These are context lines and removed lines)
            patch_source_lines = [line.value for line in hunk if line.is_context or line.is_removed]

            # --- Compare lengths first ---
            if len(source_hunk_lines) != len(patch_source_lines):
                 # This indicates a mismatch in the expected source length vs actual patch source lines
                 # Usually means the patch file itself might be malformed or unidiff interpretation issue
                 return False, (f"Hunk #{hunk_index + 1} (line {hunk.source_start}): "
                                f"Internal length mismatch. Source slice has {len(source_hunk_lines)} lines, "
                                f"Patch context/removed has {len(patch_source_lines)} lines.")

            # --- Compare line by line ---
            for i in range(len(source_hunk_lines)):
                 patch_line = patch_source_lines[i]
                 source_line = source_hunk_lines[i]

                 # *** MODIFICATION FOR DEBUGGING ***
                 if source_line != patch_line:
                     line_num = hunk.source_start + i
                     error_detail = (
                         f"Hunk #{hunk_index + 1}: Mismatch at original line {line_num}.\n"
                         f"--- Expected (from patch) ---\n{repr(patch_line)}\n"
                         f"+++ Found (in source file) +++\n{repr(source_line)}\n"
                         f"-----------------------------"
                     )
                     print("Validation Failure Detail:\n" + error_detail) # Print to console
                     # Also return the detailed message for the status update
                     return False, (f"Hunk #{hunk_index + 1}: Mismatch at original line {line_num}. "
                                    f"See console output for details.")
                 # *** END MODIFICATION ***

            # If we got here, the current hunk matches the source context

        return True, "" # All hunks validated successfully


    def _create_backup(self, full_path: str) -> str | None:
        """Creates a timestamped backup of the file. Returns backup path or None on failure."""
        if not os.path.exists(full_path):
            # Should not happen if called after existence check, but safety first
            print(f"Error: Cannot backup non-existent file: {full_path}")
            # Try to find the file_id to update status correctly
            file_id = self._find_file_id_from_full_path(full_path)
            if file_id:
                self._update_status(file_id, PatchFileStatus.FAILED, "Cannot backup non-existent file.")
            return None
        try:
            timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S_%f") # Added microseconds
            backup_path = f"{full_path}.{timestamp}.bak"
            # Ensure backup directory exists if needed (though usually same dir)
            os.makedirs(os.path.dirname(backup_path), exist_ok=True)
            shutil.copy2(full_path, backup_path) # copy2 preserves metadata
            print(f"Created backup: {os.path.basename(backup_path)}")
            return backup_path
        except Exception as e:
            error_msg = f"Failed to create backup for {os.path.basename(full_path)}: {e}"
            print(f"Error: {error_msg}\n{traceback.format_exc()}")
            # Try to find the file_id to update status correctly
            file_id = self._find_file_id_from_full_path(full_path)
            if file_id:
                self._update_status(file_id, PatchFileStatus.FAILED, f"Backup Error: {e}")
            # Show error popup as well, as this is critical
            self.root.after(0, lambda: messagebox.showerror("Backup Failed", error_msg))
            return None

    def _find_file_id_from_full_path(self, full_path: str) -> str | None:
         """Helper to find the tree item ID based on the full file path."""
         norm_full_path = os.path.normpath(full_path)
         if not self.source_folder: return None
         try:
             # Ensure source_folder also has normalized path for comparison
             norm_source_folder = os.path.normpath(self.source_folder)
             relative_path = os.path.relpath(norm_full_path, norm_source_folder)
             # Convert back to '/' separators used in patch_set and tree IDs
             file_id = relative_path.replace(os.path.sep, '/')
             # Check if this ID actually exists in our file status list
             if file_id in self.file_status:
                 return file_id
         except ValueError: # Can happen if paths are on different drives on Windows
             pass
         # Fallback: search through known file_ids by constructing full path
         for fid in self.file_status.keys():
             try:
                 check_path = os.path.normpath(os.path.join(self.source_folder, *fid.split('/')))
                 if check_path == norm_full_path:
                     return fid
             except Exception: # Ignore errors during path construction for fallback
                 continue
         print(f"Warning: Could not map full path '{full_path}' back to a known file ID.")
         return None # Not found


    def _restore_selected(self, selected_ids: list[str]):
        """Restores selected files from the latest backup."""
        restored_count = 0
        failed_count = 0
        no_backup_count = 0

        processed_count = 0
        total_to_process = len(selected_ids)

        print(f"--- Starting Restore ({total_to_process} selected files) ---")

        for file_id in selected_ids:
            processed_count += 1
            print(f"--- Restore ({processed_count}/{total_to_process}): Processing '{file_id}' ---")

            patched_file = self._find_patched_file(file_id)
            if not patched_file or not self.source_folder:
                 print("Skipping: Patch data or source folder missing.")
                 failed_count +=1 # Count as failed if we can't process it
                 continue

            try:
                full_path = os.path.normpath(os.path.join(self.source_folder, *file_id.split('/')))
                base_name = os.path.basename(full_path)
                dir_name = os.path.dirname(full_path)

                if not os.path.exists(dir_name):
                    print(f"Directory '{dir_name}' does not exist. Cannot find backups.")
                    no_backup_count += 1
                    self._update_status(file_id, self.file_status.get(file_id, PatchFileStatus.FAILED), "Parent directory missing.")
                    continue

                # Find the chronologically latest backup file in the target directory
                backups = sorted([
                    f for f in os.listdir(dir_name)
                    # Check if it starts with the base name + '.' and ends with '.bak'
                    if f.startswith(base_name + '.') and f.endswith('.bak')
                ], key=lambda x: os.path.getmtime(os.path.join(dir_name, x)), reverse=True) # Sort by modification time

                if not backups:
                    msg = "No backup file found."
                    print(msg)
                    self._update_status(file_id, self.file_status.get(file_id, PatchFileStatus.FAILED), msg)
                    no_backup_count += 1
                    continue

                latest_backup_name = backups[0]
                latest_backup_path = os.path.join(dir_name, latest_backup_name)
                print(f"Found latest backup: '{latest_backup_name}'")
                print(f"Action: Restoring '{file_id}' from backup.")
                shutil.copy2(latest_backup_path, full_path) # Restore by copying backup over current
                self._update_status(file_id, PatchFileStatus.RESTORED, f"Restored from {latest_backup_name}")
                restored_count += 1
                print("Successfully restored.")

            except FileNotFoundError:
                # This might happen if the original file was deleted AND no backup exists
                 msg = "Restore failed: Original or backup file missing."
                 print(f"Error: {msg}")
                 self._update_status(file_id, PatchFileStatus.FAILED, msg)
                 failed_count += 1
            except Exception as e:
                 error_msg = f"Failed to restore {file_id}: {e}"
                 print(f"Error: {error_msg}\n{traceback.format_exc()}")
                 self._update_status(file_id, PatchFileStatus.FAILED, f"Restore Error: {e}")
                 failed_count += 1

        # --- Final Summary Message ---
        message = (f"Restore attempt finished for {total_to_process} selected file(s):\n\n")
        if restored_count > 0: message += f"- Restored: {restored_count}\n"
        if failed_count > 0: message += f"- Failed: {failed_count}\n"
        if no_backup_count > 0: message += f"- No Backup Found: {no_backup_count}\n"

        self.root.after(0, lambda: messagebox.showinfo("Restore Complete", message))
        print("--- Restore Finished ---")


    def _update_status(self, file_id: str, status: PatchFileStatus, details: str | None = None):
        """Updates the Treeview and internal status, logs details."""
        # Ensure the update happens on the main thread
        def do_update():
            try:
                 # Check if item exists before trying to set
                 if self.tree.exists(file_id):
                     # Get current values to preserve others if needed
                     current_values = list(self.tree.item(file_id, 'values'))
                     if len(current_values) >= 2:
                         current_values[1] = status.value # Update status column
                         self.tree.item(file_id, values=tuple(current_values))
                     else: # Fallback if values somehow incorrect
                          self.tree.set(file_id, 'status', status.value)

                     self.file_status[file_id] = status # Update internal state

                     # Optionally, log the detail message associated with the status change
                     # Note: details might already be printed in the calling function
                     # if details:
                     #     print(f"Status Updated [{file_id}]: {status.value} - {details}")
                     # else:
                     #     print(f"Status Updated [{file_id}]: {status.value}")

                 else:
                     print(f"Warning: Attempted to update status for non-existent tree item '{file_id}'")
            except Exception as e:
                 # Catch errors during Tkinter update if GUI closed prematurely etc.
                 print(f"Error updating GUI for {file_id}: {e}")

        # Check if the root window still exists before queueing UI update
        if hasattr(self.root, 'winfo_exists') and self.root.winfo_exists():
             self.root.after(0, do_update)
        else:
             # If window is closed, just print the status update attempt
             print(f"Status (GUI closed) [{file_id}]: {status.value}" + (f" - {details}" if details else ""))


    def preview_selected_patch(self, event=None):
         """Shows patch hunks in the left pane and tries to show original file content."""
         selected = self.tree.selection()
         if not selected:
             self.text_original.delete(1.0, 'end')
             self.text_patched.delete(1.0, 'end')
             return

         selected_file_id = selected[0]
         patch = self._find_patched_file(selected_file_id)

         # Clear preview panes
         self.text_original.config(state='normal') # Ensure editable
         self.text_patched.config(state='normal')
         self.text_original.delete(1.0, 'end')
         self.text_patched.delete(1.0, 'end')

         if not patch:
             self.text_original.insert('end', f"Error: Could not find patch data for '{selected_file_id}'.")
             self.text_original.config(state='disabled')
             self.text_patched.config(state='disabled')
             return

         # --- Left Pane: Try to show Original File Content ---
         original_content = f"--- Source File: {selected_file_id} ---\n"
         full_path = None
         if self.source_folder:
             try:
                 full_path = os.path.normpath(os.path.join(self.source_folder, *selected_file_id.split('/')))
                 if os.path.exists(full_path) and not os.path.isdir(full_path):
                      # Try reading with common encodings
                      read_success = False
                      for enc in ['utf-8', 'latin-1', 'cp1252']:
                          try:
                              with open(full_path, 'r', encoding=enc) as f_orig:
                                  original_content += f_orig.read()
                              read_success = True
                              break
                          except UnicodeDecodeError:
                              continue
                          except Exception as read_e:
                              original_content += f"\nError reading file ({enc}): {read_e}"
                              break # Stop trying on other errors
                      if not read_success:
                           original_content += "\nError: Could not decode file content."
                 elif patch.is_added_file:
                      original_content += "\n(File does not exist - will be created by patch)"
                 else:
                      original_content += "\n(File does not exist or is not accessible)"
             except Exception as path_e:
                 original_content += f"\nError constructing path: {path_e}"
         else:
             original_content += "\n(Select source folder to view file content)"

         self.text_original.insert('end', original_content)

         # --- Right Pane: Show Patch Details ---
         target_content = f"--- Patch Details for: {selected_file_id} ---\n"
         target_content += f"Source: {patch.source_file}\nTarget: {patch.target_file}\n"

         if patch.is_added_file:
              target_content += "\nType: New File\n"
              target_content += f"+{patch.added} lines\n"
              target_content += "--- Hunks (showing added content) ---\n"
              for hunk in patch:
                   for line in hunk:
                        target_content += line.value # Show added content directly
         elif patch.is_removed_file:
               target_content += "\nType: File Removal\n"
               target_content += f"-{patch.removed} lines\n"
               target_content += "--- Hunks (showing removed content) ---\n"
               for hunk in patch:
                    for line in hunk:
                         target_content += line.value # Show removed content directly
         elif len(patch) > 0 : # Modified file with hunks
             target_content += "\nType: Modified File\n"
             target_content += f"+{patch.added} -{patch.removed} ({len(patch)} hunks)\n"
             target_content += "--- Hunks (raw patch format) ---\n"
             for hunk_index, hunk in enumerate(patch):
                 target_content += f"\n--- Hunk #{hunk_index + 1} ---\n"
                 target_content += str(hunk) # Use unidiff's hunk string representation
         else: # No changes specified
              target_content += "\n(No changes specified in patch for this file)"

         self.text_patched.insert('end', target_content)

         # Make panes read-only after inserting
         self.text_original.config(state='disabled')
         self.text_patched.config(state='disabled')


    def _update_preview_content(self, file_id, original_content, patched_content):
         """Updates preview panes, typically after a successful application."""
         # Check if the currently selected item is the one we just processed
         selected = self.tree.selection()
         if selected and selected[0] == file_id:
             self.text_original.config(state='normal')
             self.text_patched.config(state='normal')
             self.text_original.delete(1.0, 'end')
             self.text_original.insert('end', original_content)
             self.text_patched.delete(1.0, 'end')
             self.text_patched.insert('end', patched_content)
             self.text_original.config(state='disabled')
             self.text_patched.config(state='disabled')


    def show_prompt(self):
        # (Keep your existing ChatGPT prompt logic if needed, maybe update it)
        failed_files = [fid for fid, status in self.file_status.items()
                        if status in (PatchFileStatus.FAILED, PatchFileStatus.MISMATCH)]
        prompt = (
            "ChatGPT, I attempted to apply a patch using a tool, but it failed for the following file(s):\n"
        )
        if failed_files:
             prompt += "- " + "\n- ".join(failed_files) + "\n\n"
             prompt += ("The error often indicates a mismatch between the patch's context lines "
                        "and the actual content of the source file(s). This could be due to "
                        "line ending differences (LF vs CRLF), whitespace changes, or other modifications.\n\n"
                        "Please carefully regenerate the patch in raw unified diff format for these files. "
                        "Ensure the context lines exactly match the original source code structure and content "
                        "that the patch should apply to. Use standard LF line endings.\n"
                        "Do not include markdown formatting.\n\n"
                        "Original source code structure (if needed for context) might be provided separately.\n"
                        "Goal: Create a patch that the `unidiff` library or standard `patch` command can apply cleanly.")
        else:
            prompt += "(No specific files failed, but review general patch format)\n\n"
            prompt += ("Please generate a patch in raw unified diff format.\n"
                       "Ensure context lines match source, use LF endings, and avoid markdown.")

        try:
            self.root.clipboard_clear()
            self.root.clipboard_append(prompt)
            messagebox.showinfo("Prompt Copied", "Diagnostic prompt copied to clipboard. Paste it into ChatGPT along with relevant source code snippets if needed.")
        except tk.TclError:
             messagebox.showwarning("Clipboard Error", "Could not access clipboard.")


    def show_raw_patch(self):
        if not self.raw_patch_text:
            messagebox.showinfo("No Patch Loaded", "Please load a patch file first.")
            return

        raw_win = Toplevel(self.root)
        raw_win.title("Raw Patch Content")
        raw_win.geometry("700x500") # Give it a decent size

        text_frame = ttk.Frame(raw_win, padding=5)
        text_frame.pack(fill='both', expand=True)

        box = ScrolledText(text_frame, wrap='none', width=100, height=30) # Use wrap='none'
        box.pack(side='left', fill='both', expand=True)

        scrollbar = ttk.Scrollbar(text_frame, orient='vertical', command=box.yview)
        scrollbar.pack(side='right', fill='y')
        box['yscrollcommand'] = scrollbar.set

        # Add horizontal scrollbar too
        h_scrollbar = ttk.Scrollbar(raw_win, orient='horizontal', command=box.xview)
        h_scrollbar.pack(side='bottom', fill='x', padx=5, pady=(0,5))
        box['xscrollcommand'] = h_scrollbar.set

        box.insert('end', self.raw_patch_text)
        box.configure(state='disabled') # Make read-only

        def copy_to_clipboard():
            try:
                self.root.clipboard_clear()
                self.root.clipboard_append(self.raw_patch_text)
                messagebox.showinfo("Copied", "Raw patch content copied to clipboard.", parent=raw_win)
            except tk.TclError:
                messagebox.showwarning("Clipboard Error", "Could not access clipboard.", parent=raw_win)

        Button(raw_win, text="Copy Patch to Clipboard", command=copy_to_clipboard).pack(pady=5)
        raw_win.transient(self.root) # Keep it on top of the main window
        raw_win.grab_set() # Modal behavior
        self.root.wait_window(raw_win) # Wait until closed


    def export_log(self):
        if not self.file_status:
            messagebox.showinfo("Nothing to Export", "No patch results to export.")
            return
        file_path = filedialog.asksaveasfilename(defaultextension=".log",
                                                filetypes=[("Log files", "*.log"), ("Text files", "*.txt")],
                                                title="Save Patch Log")
        if not file_path:
            return
        try:
            with open(file_path, 'w', encoding='utf-8') as f:
                f.write(f"Patch Log Exported: {datetime.datetime.now()}\n")
                f.write(f"Source Folder: {self.source_folder or 'Not Set'}\n")
                # Consider adding Patch file name if available
                # patch_filename = os.path.basename(self.patch_file_path) if hasattr(self, 'patch_file_path') else "Unknown"
                # f.write(f"Patch File: {patch_filename}\n")
                f.write("-" * 20 + "\n")
                # Sort by file path for consistency
                sorted_items = sorted(self.file_status.items())
                for file_id, status in sorted_items:
                    # Get summary info from tree if available and item exists
                    summary = ""
                    try: # Add try-except for safety if tree items are removed
                         if self.tree.exists(file_id):
                             values = self.tree.item(file_id, 'values')
                             if len(values) >= 3:
                                 summary = f" ({values[2]})" # Add summary like "+10 -5"
                    except tk.TclError:
                         summary = " (Tree item gone)" # Indicate if item disappeared

                    f.write(f"{status.value:<18}: {file_id}{summary}\n") # Align status column better

            messagebox.showinfo("Export Complete", f"Patch log saved to:\n{file_path}")
        except Exception as e:
            messagebox.showerror("Export Failed", f"Could not save log file:\n{e}\n{traceback.format_exc()}")

# --- Main Execution ---
if __name__ == '__main__':
    # Basic check for Tkinter availability
    try:
        root = Tk()
    except tk.TclError as e:
        print(f"Error initializing Tkinter: {e}")
        print("Ensure you have a graphical environment and Tkinter is correctly installed.")
        exit(1)

    # Optional: Add some padding around the main window
    root.geometry("900x700") # Adjust initial size as needed
    # Center the window (optional)
    # root.eval('tk::PlaceWindow . center')
    app = PatchApplierGUI(root)
    root.mainloop()