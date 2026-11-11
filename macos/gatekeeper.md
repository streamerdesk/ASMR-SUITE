## 🔓 Si macOS dice «no se puede abrir» (Gatekeeper)
Los plugins y el instalador están firmados *ad-hoc* pero **no notarizados** por Apple (somos un proyecto gratuito), así que la primera vez macOS puede avisarte:

1. **Clic derecho (o Ctrl-clic) > Abrir** sobre `Instalar - Install.command` (o sobre el `.pkg`) y confirma **Abrir**.
2. Si aun así se bloquea: **Ajustes del Sistema > Privacidad y seguridad**, baja hasta el aviso del archivo y pulsa **Abrir igualmente**; después vuelve a abrirlo.
3. El instalador quita la cuarentena de los plugins, así que OBS los cargará sin más avisos. Si copias el `.plugin` a mano, hazlo tú en el Terminal:
   `xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/*.plugin`

## 🔓 If macOS says it "can't be opened" (Gatekeeper)
The plugins and installer are *ad-hoc* signed but **not notarized** by Apple (this is a free project), so the first time macOS may warn you:

1. **Right-click (or Control-click) > Open** on `Instalar - Install.command` (or the `.pkg`) and confirm **Open**.
2. If it is still blocked: **System Settings > Privacy & Security**, scroll down to the message about the file and click **Open Anyway**, then open it again.
3. The installer removes the quarantine flag from the plugins, so OBS loads them without further prompts. If you copy a `.plugin` by hand, run this in Terminal:
   `xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/*.plugin`
