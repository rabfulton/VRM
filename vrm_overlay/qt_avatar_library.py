"""Qt adapter for the local avatar library; copying and ZIP work stay off the UI thread."""
from pathlib import Path
import threading
import zipfile
import zlib

from PySide6.QtCore import QObject, Property, QStandardPaths, QUrl, Signal, Slot
from PySide6.QtGui import QDesktopServices

from .avatar_catalog import featured_avatars
from .avatar_library import AvatarStore


class AvatarLibrary(QObject):
    changed = Signal()
    imported = Signal(str)
    _finished = Signal(object, str)

    def __init__(self, parent=None, *, root=None):
        super().__init__(parent)
        self._store = AvatarStore(root or Path(QStandardPaths.writableLocation(QStandardPaths.AppDataLocation)) / 'avatars')
        self._featured = featured_avatars()
        self._installed = self._store.entries()
        self._busy = False
        self._error = ''
        self._status = ''
        self._choices = []
        self._archive = None
        self._source = None
        self._thread = None
        self._cancelled = threading.Event()
        self._closing = False
        self._finished.connect(self._complete)

    featured = Property('QVariantList', lambda self: self._featured, constant=True)
    installed = Property('QVariantList', lambda self: self._installed, notify=changed)
    choices = Property('QStringList', lambda self: self._choices, notify=changed)
    busy = Property(bool, lambda self: self._busy, notify=changed)
    error = Property(str, lambda self: self._error, notify=changed)
    status = Property(str, lambda self: self._status, notify=changed)
    folder = Property(str, lambda self: str(self._store.root), constant=True)

    @Slot()
    def refresh(self):
        self._installed = self._store.entries()
        self.changed.emit()

    @Slot(str)
    def openCreator(self, identity):
        # Only the curated HTTPS pages can be launched, never arbitrary model metadata.
        entry = next((e for e in self._featured if e['id'] == identity), None)
        if entry and not QDesktopServices.openUrl(QUrl(entry['source_url'])):
            self._error = 'Could not open your browser. Creator page: ' + entry['source_url']
            self.changed.emit()

    @Slot(str)
    def openTerms(self, identity):
        entry = next((e for e in self._installed if e['id'] == identity), None)
        url = QUrl(entry['license_url']) if entry else QUrl()
        if url.scheme() == 'https' and url.host() and not QDesktopServices.openUrl(url):
            self._error = 'Could not open your browser. License page: ' + url.toString()
            self.changed.emit()

    def _run(self, task, status):
        if self._busy or self._closing:
            return
        self._busy = True
        self._error = ''
        self._status = status
        self.changed.emit()

        def work():
            try:
                result, error = task(), ''
            except (OSError, ValueError, KeyError, TypeError, IndexError, AttributeError,
                    RuntimeError, zipfile.BadZipFile, zlib.error) as exc:
                result, error = None, str(exc) or 'Could not import this avatar.'
            if not self._closing:
                self._finished.emit(result, error)

        self._thread = threading.Thread(target=work, name='avatar-import', daemon=True)
        self._thread.start()

    @Slot(str, str)
    def importFile(self, value, source_id=''):
        if self._busy or self._closing:
            return
        url = QUrl(value)
        if url.scheme() and not url.isLocalFile():
            self._error = 'Choose a downloaded local file.'
            self.changed.emit()
            return
        path = Path(url.toLocalFile() if url.isLocalFile() else value).expanduser().resolve()
        self._choices = []
        self._archive = None
        self._source = next((e for e in self._featured if e['id'] == source_id and e['kind'] == 'featured'), None)
        if path.suffix.lower() == '.zip':
            self._archive = path
            self._run(lambda: dict(choices=self._store.archive_models(path)), 'Reading ZIP…')
        else:
            self._run(lambda: self._store.install(path, source=self._source, cancelled=self._cancelled), 'Importing avatar…')

    @Slot(str)
    def importVariant(self, member):
        if not self._archive or member not in self._choices or self._busy:
            return
        path, source = self._archive, self._source
        self._choices = []
        self._run(lambda: self._store.install(path, member=member, source=source, cancelled=self._cancelled), 'Importing variant…')

    @Slot()
    def cancelVariants(self):
        if not self._busy:
            self._choices = []
            self._archive = None
            self._status = ''
            self.changed.emit()

    @Slot(object, str)
    def _complete(self, result, error):
        if self._closing:
            return
        self._busy = False
        self._error = error
        if error:
            self._status = ''
            self.changed.emit()
        elif 'choices' in result:
            self._choices = result['choices']
            if len(self._choices) == 1:
                self.importVariant(self._choices[0])
            else:
                self._status = 'Choose a model from the ZIP'
                self.changed.emit()
        else:
            self._installed = self._store.entries()
            self._status = 'Added to My avatars · ready to use'
            self.changed.emit()
            self.imported.emit(result['id'])

    def shutdown(self):
        self._closing = True
        self._cancelled.set()
        if self._thread:
            self._thread.join(timeout=1)
