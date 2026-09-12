# Review retained edits

The standalone and embedded editors provide **Review retained edits** at the lower right of the editor. Records appear in phoneme, unit and seam order.

1. Finish or cancel any active text edit, then open the review. If text composition is still active, opening is rejected and your draft is preserved. Opening the review does not change the song.
2. Use **Previous edit / Next edit** to inspect retained phoneme data, unit ID/renderer/span, or seam settings.
3. Use **Next sound** to choose a current target. The first choice starts near the retained note; **Previous sound / Next sound** move through available targets. No target is selected on opening or when changing retained edits.
4. Review the displayed target key and sound, then choose **Apply binding**. Unit targets show the last covered sound; seam targets show the preceding sound (or initial boundary). This preserves the retained edit payload and binds it to that current target in one undo step.
5. **Close** or press **Escape**. Use the normal Undo command to restore the previous edit.

Tab/Shift-Tab moves among enabled review controls; Enter activates the focused control. Background editor input is blocked while reviewing. Resolver warnings explain unavailable targets. Stale source/target changes or occupied destinations are rejected without modifying the song; close and reopen to refresh the review.

Unit spans must fit the current sequence and cannot overlap another active or retained unit edit. Unsupported or unresolved pronunciation anywhere in a unit span, or on either side of a seam boundary, prevents rebinding. Rejected targets do not discard retained data.

Rebinding establishes a pronunciation address; it does not certify that a saved unit ID exists in the installed voicebank or qualify singing quality. The workflow does not automatically resolve all retained work. Legacy payloads remain available even when they cannot currently be applied.
