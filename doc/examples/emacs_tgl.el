; Example integration of TGL into Emacs.
; Tested on: GNU Emacs 23

(defun invoke-tgl-on-region (with-prefix)
"Invokes tgl on the current region, or the current line if the region is not
in use.

If with-prefix is non-nil, pass -p to tgl to look for prefix data."
 (interactive "P")
 (let ((begin
        (if (use-region-p) (region-beginning)
          (save-excursion
            (beginning-of-line)
            (point))))
       (end
        (if (use-region-p) (region-end)
          (save-excursion
            (end-of-line)
            (point)))))
   (shell-command-on-region begin end
                            (concat (if with-prefix "tgl -p" "tgl")
                                    " -c "
                                    (shell-quote-argument
                                     (or (buffer-file-name)
                                         "")))
                            t t
                            "* Tgl Errors *" t)
   ; Mark is left at the end of the output.
   ; The user probably doesn't want the new mark, and definitely wants mark
   ; after the output.
   (exchange-point-and-mark t)
   (pop-mark)))

(defun smart-invoke-tgl-and-newline-and-indent (with-prefix)
"Call invoke-tgl-on-region, then either indent (if at BOL) or
newline-and-indent otherwise."
  (interactive "P")
  (invoke-tgl-on-region with-prefix)
  (if (= (point)
         (save-excursion (beginning-of-line) (point)))
      (indent-according-to-mode)
    (newline-and-indent)))

; Bind smart-invoke-tgl-and-newline-and-indent to C-M-j
(global-set-key "\e\C-j" 'smart-invoke-tgl-and-newline-and-indent)

; Some modes (like the c-mode family) rebind M-j and C-M-j back to the same
; thing. Prevent that from happening.
(add-hook 'after-change-major-mode-hook
          (lambda () (local-unset-key "\e\C-j")))
