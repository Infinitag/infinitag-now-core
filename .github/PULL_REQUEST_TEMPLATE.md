<!-- PR-Titel im Format: "feat: …", "fix: …", "docs: …", "refactor: …",
     "chore: …" – der Titel landet als Squash-Commit auf main und in den
     Release-Notes. Genau EIN Typ-Label setzen (steuert die Gruppierung
     der automatischen Release-Notes, siehe .github/release.yml). -->

## Typ

- [ ] ✨ Feature (Label `enhancement`)
- [ ] 🐛 Fix (Label `bug`)
- [ ] 📝 Doku (Label `documentation`)
- [ ] ♻️ Refactor (Label `refactor`)
- [ ] 🔧 Chore / Build / Tooling (Label `chore`)
- [ ] ⚠️ Protokoll-/Breaking-Änderung (Label `protocol`)

## Was ändert sich?

-

## Warum?

-

## Protokoll-Checkliste (bei Label `protocol`)

- [ ] `PROTOCOL.md` im selben PR aktualisiert
- [ ] Unit-Tests angepasst/ergänzt (`pio test -e native` grün)
- [ ] Kompatibel ohne `version`-Byte-Bump? Falls nein: Byte erhöht +
      Major-Tag geplant, Geräte-Repos folgen

## Getestet

- [ ] `pio test -e native` grün
- [ ] Geräte-Firmwares (Station/Config-Box) bauen gegen diesen Stand
