import React, { useState, useEffect } from 'react'
import {
  Settings,
  CloudDownload,
  Cpu,
  LayoutDashboard,
  Database,
  RefreshCw,
  Package,
  Heart,
  Menu
} from 'lucide-react'

import './App.css'

// Utilities
import { cn, isPS5 } from './utils/helpers'

// UI Components
import Toast from './components/ui/Toast'
import Modal from './components/ui/Modal'
import NavButton from './components/ui/NavButton'
import PayloadButton from './components/ui/PayloadButton'

// Views
import StorageHub from './components/views/StorageHub'
import AutoloadView from './components/views/AutoloadView'
import SettingsView from './components/views/SettingsView'
import DonateView from './components/views/DonateView'
import AutoloadOverlay from './components/views/AutoloadOverlay'

function App() {
  const [view, setView] = useState('dashboard')
  const [sidebarExpanded, setSidebarExpanded] = useState(true)
  const [autoloadStatus, setAutoloadStatus] = useState(null)
  const [logs, setLogs] = useState([])
  const [payloads, setPayloads] = useState([])
  const [config, setConfig] = useState({})
  const [ip, setIp] = useState('0.0.0.0')
  const [version, setVersion] = useState('Loading...')
  const [loading, setLoading] = useState(false)
  const [activeLoadingName, setActiveLoadingName] = useState('')
  const [toasts, setToasts] = useState([])
  const [loadingPayloads, setLoadingPayloads] = useState(true)
  const [downloadModal, setDownloadModal] = useState({ show: false, name: '', progress: 0 })
  const [confirmModal, setConfirmModal] = useState({ show: false, title: '', message: '', onConfirm: null })

  const addToast = (message, type = 'success') => {
    const id = Date.now()
    setToasts(prev => [...prev, { id, message, type }])
  }

  const removeToast = (id) => {
    setToasts(prev => prev.filter(t => t.id !== id))
  }

  const api = async (endpoint, options = {}) => {
    try {
      const response = await fetch(endpoint, options)
      if (options.method === 'POST') return response.text()
      try {
        const text = await response.text()
        if (text.toLowerCase().includes('<!doctype')) return null
        return JSON.parse(text)
      } catch (e) { return null }
    } catch (e) { return null }
  }

  const refreshPayloads = async (retryCount = 0) => {
    setLoadingPayloads(true)
    const data = await api('/list_payloads')
    if (data?.payloads) {
      // Sort payloads: internal first, USB last
      const sorted = [...data.payloads].sort((a, b) => {
        const aIsUsb = a.startsWith('/mnt/usb')
        const bIsUsb = b.startsWith('/mnt/usb')
        if (aIsUsb && !bIsUsb) return 1
        if (!aIsUsb && bIsUsb) return -1
        return a.localeCompare(b)
      })
      setPayloads(sorted)
      setLoadingPayloads(false)
    } else if (retryCount < 5) {
      setTimeout(() => refreshPayloads(retryCount + 1), 1000)
    } else {
      setLoadingPayloads(false)
    }
  }

  const refreshConfig = async () => {
    const data = await api('/get_config')
    if (data) setConfig(data)
  }

  const handleAbort = async () => {
    await fetch('/abort').catch(() => { })
    setAutoloadStatus(prev => prev ? { ...prev, remaining: -1 } : null)
    addToast("Sequence Aborted", "error")
  }

  const handleFinish = async () => {
    await fetch('/autoload_clear').catch(() => { })
    setAutoloadStatus(null)
    window.location.reload()
  }

  const loadPayload = async (path) => {
    const name = path.split('/').pop().replace(/\.(elf|bin|lua)$/i, '').replace(/_/g, ' ')
    setLoading(true)
    setActiveLoadingName(name)
    try {
      await fetch(`/loadpayload:${path}`)
      addToast(`${name} launched`)
    } catch (e) { addToast("Launch failed", "error") }
    setTimeout(() => {
      setLoading(false)
      setActiveLoadingName('')
    }, 1500)
  }

  const handleDelete = (fileName) => {
    setConfirmModal({
      show: true,
      title: "Delete Payload",
      message: `Are you sure you want to remove ${fileName}?`,
      onConfirm: async () => {
        setConfirmModal({ show: false })
        await fetch(`/manage:delete?filename=${encodeURIComponent(fileName)}`)
        refreshPayloads()
        addToast(`${fileName} removed`)
      }
    })
  }

  const handleUpload = async (e) => {
    const file = e.target.files[0]
    if (!file) return
    setDownloadModal({ show: true, name: file.name, progress: 20 })
    try {
      await fetch(`/manage:upload?filename=${encodeURIComponent(file.name)}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: file
      })
      setDownloadModal(prev => ({ ...prev, progress: 100 }))
      addToast(`${file.name} uploaded`)
      refreshPayloads()
    } catch (e) { addToast("Upload failed", "error") }
    setTimeout(() => setDownloadModal({ show: false }), 800)
  }

  const handleInstall = async (p) => {
    setDownloadModal({ show: true, name: p.filename, progress: 10 })
    try {
      const elfRes = await fetch(p.url)
      if (!elfRes.ok) throw new Error('Failed to fetch payload from source')
      setDownloadModal(prev => ({ ...prev, progress: 50 }))

      const buffer = await elfRes.arrayBuffer()
      setDownloadModal(prev => ({ ...prev, progress: 70 }))

      const pushRes = await fetch(
        `/repository_install_push?filename=${encodeURIComponent(p.filename)}`,
        { method: 'POST', body: buffer }
      )
      setDownloadModal(prev => ({ ...prev, progress: 90 }))

      const data = await pushRes.json().catch(() => null)
      if (pushRes.ok && data?.ok) {
        setDownloadModal(prev => ({ ...prev, progress: 100 }))
        addToast(`${p.filename} installed`)
        refreshPayloads()
      } else throw new Error(data?.message || 'Install failed')
    } catch (e) { addToast(e.message || 'Installation failed', 'error') }
    setTimeout(() => setDownloadModal({ show: false }), 800)
  }

  const handleSaveConfig = async (newConfig) => {
    const merged = { ...config, ...newConfig }
    const success = await api('/set_config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(merged)
    })
    if (success) {
      refreshConfig()
      return true
    } else {
      addToast("Save failed", "error")
      return false
    }
  }

  useEffect(() => {
    const init = async () => {
      const ipRes = await fetch('/getip').then(r => r.text()).catch(() => '0.0.0.0')
      setIp(ipRes.toLowerCase().includes('<!doctype') ? '192.168.1.133' : ipRes)
      const verRes = await fetch('/version').then(r => r.text()).catch(() => '?')
      setVersion(verRes.toLowerCase().includes('<!doctype') ? '1.0.0-dev' : verRes)
      refreshPayloads()
      refreshConfig()
    }
    init()
  }, [])

  useEffect(() => {
    if (view === 'autoload' || view === 'storage') {
      refreshConfig()
      refreshPayloads()
    }
  }, [view])

  useEffect(() => {
    let statusTimeout
    const poll = async () => {
      try {
        const res = await fetch('/autoload_status')
        if (res.ok) {
          const data = await res.json()
          setAutoloadStatus(data)

          // Poll as long as countdown is active OR sequence is executing (not yet DONE)
          const isActive = data && (data.remaining >= 0 && data.current !== 'DONE')
          if (isActive) {
            // Poll faster during active execution
            const delay = (data.remaining > 0) ? 1000 : 500
            statusTimeout = setTimeout(poll, delay)
          }
        }
      } catch (e) { }
    }
    poll()
    return () => clearTimeout(statusTimeout)
  }, [])


  return (
    <div className={cn(
      "min-h-screen min-h-[100dvh] ps5-bg text-zinc-100 font-ps5 flex",
      isPS5 ? "flex-row overflow-hidden" : "flex-col md:flex-row md:overflow-hidden"
    )}>
      {/* Toast Container */}
      <div className="fixed top-0 right-0 p-8 z-[2000] space-y-4 pointer-events-none">
        {toasts.map(t => (
          <Toast key={t.id} message={t.message} type={t.type} onClose={() => removeToast(t.id)} />
        ))}
      </div>

      {/* Modals */}
      <Modal show={downloadModal.show} title="Processing Payload" onClose={() => { }}>
        <div className="space-y-6">
          <div className="flex justify-between items-end">
            <span className="text-ps-blue font-black uppercase italic tracking-tighter text-2xl">{downloadModal.name}</span>
            <span className="text-white font-bold text-xl">{downloadModal.progress}%</span>
          </div>
          <div className="h-4 bg-white/5 rounded-full overflow-hidden border border-white/10 p-0.5">
            <div className="h-full bg-ps-blue rounded-full transition-all duration-500 shadow-[0_0_20px_rgba(0,112,209,0.5)]" style={{ width: `${downloadModal.progress}%` }} />
          </div>
        </div>
      </Modal>

      <Modal
        show={confirmModal.show}
        title={confirmModal.title}
        onClose={() => setConfirmModal({ show: false })}
        footer={
          <>
            <button onClick={() => setConfirmModal({ show: false })} className="flex-1 px-8 py-5 rounded-2xl bg-white/5 hover:bg-white/10 text-white font-bold transition-all uppercase tracking-tight">Cancel</button>
            <button onClick={confirmModal.onConfirm} className="flex-1 px-8 py-5 rounded-2xl bg-red-600 hover:bg-red-500 text-white font-bold transition-all uppercase tracking-tight shadow-xl shadow-red-900/20">Confirm</button>
          </>
        }
      >
        {confirmModal.message}
      </Modal>

      {autoloadStatus && autoloadStatus.remaining >= 0 && (
        <AutoloadOverlay status={autoloadStatus} onCancel={handleAbort} onFinish={handleFinish} isPS5={isPS5} />
      )}

      {/* DESKTOP SIDEBAR */}
      <aside className={cn(
        "flex-col bg-black/40 backdrop-blur-3xl border-r border-white/5 transition-all duration-500 z-[100] h-screen shadow-[10px_0_30px_rgba(0,0,0,0.5)]",
        isPS5 ? "flex" : "hidden md:flex",
        sidebarExpanded ? "w-80" : "w-24"
      )}>
        <div className="p-6 flex flex-col h-full">
          <div className="flex items-center mb-12 h-10">
            <button
              onClick={() => setSidebarExpanded(!sidebarExpanded)}
              className="p-3 bg-white/5 hover:bg-ps-blue hover:text-white rounded-xl transition-all mr-4 shrink-0"
            >
              <Menu className="w-6 h-6" />
            </button>
            <div className={cn("flex items-center space-x-3 transition-all duration-500", sidebarExpanded ? "opacity-100 scale-100" : "opacity-0 scale-90 absolute pointer-events-none")}>
              <div className="p-2 bg-ps-blue rounded-xl shadow-[0_0_20px_rgba(0,112,209,0.3)]">
                <Cpu className="w-6 h-6 text-white" />
              </div>
              <span className="text-2xl font-bold tracking-tight text-white">Next<span className="text-ps-blue">Menu</span></span>
            </div>
          </div>

          <nav className="flex-1 space-y-2">
            <NavButton sidebar sidebarExpanded={sidebarExpanded} active={view === 'dashboard'} onClick={() => setView('dashboard')} icon={LayoutDashboard} label="Dashboard" />
            <NavButton sidebar sidebarExpanded={sidebarExpanded} active={view === 'storage'} onClick={() => setView('storage')} icon={Database} label="Manage Payloads" />
            <NavButton sidebar sidebarExpanded={sidebarExpanded} active={view === 'autoload'} onClick={() => setView('autoload')} icon={RefreshCw} label="Autoload" />
            <NavButton sidebar sidebarExpanded={sidebarExpanded} active={view === 'settings'} onClick={() => setView('settings')} icon={Settings} label="Settings" />
          </nav>

          <div className="pt-6 border-t border-white/5">
            <NavButton
              sidebar
              sidebarExpanded={sidebarExpanded}
              active={view === 'donate'}
              onClick={() => setView('donate')}
              icon={Heart}
              label="Donate"
              className={view === 'donate' ? "bg-red-600 shadow-[0_0_20px_rgba(220,38,38,0.3)]" : "text-red-500 hover:bg-red-600/10"}
            />
          </div>
        </div>
      </aside>

      {/* MOBILE BOTTOM NAV */}
      <nav className={cn(
        "fixed bottom-0 inset-x-0 z-[100] bg-black/80 backdrop-blur-2xl border-t border-white/5 h-[calc(5rem+env(safe-area-inset-bottom))] pb-[env(safe-area-inset-bottom)] flex items-center shadow-[0_-10px_30px_rgba(0,0,0,0.5)]",
        isPS5 ? "hidden" : "md:hidden"
      )}>
        <NavButton active={view === 'dashboard'} onClick={() => setView('dashboard')} icon={LayoutDashboard} label="Dashboard" mobileLabel="HOME" />
        <NavButton showSeparator active={view === 'storage'} onClick={() => setView('storage')} icon={Database} label="Manage Payloads" mobileLabel="MANAGE" />
        <NavButton showSeparator active={view === 'autoload'} onClick={() => setView('autoload')} icon={RefreshCw} label="Autoload" mobileLabel="AUTO" />
        <NavButton showSeparator active={view === 'settings'} onClick={() => setView('settings')} icon={Settings} label="Settings" mobileLabel="SETTINGS" />
        <NavButton
          showSeparator
          active={view === 'donate'}
          onClick={() => setView('donate')}
          icon={Heart}
          label="Donate"
          mobileLabel="DONATE"
        />
      </nav>

      {/* MAIN CONTENT AREA */}
      <div className={cn(
        "flex flex-col relative",
        isPS5 ? "h-screen flex-1 min-h-0" : "md:h-screen md:flex-1 md:min-h-0"
      )}>
        <main className={cn(
          "custom-scrollbar pb-28 md:pb-12 max-w-[1800px] mx-auto w-full flex flex-col",
          isPS5 ? "pt-16 px-16 flex-1 overflow-y-auto" : "pt-6 px-6 md:pt-16 md:px-16 md:flex-1 md:overflow-y-auto"
        )}>
          {view === 'dashboard' && (
            <div className="space-y-8 md:space-y-12">
              <h2 className="text-4xl font-extrabold text-white tracking-tight">
                Launch <span className="text-ps-blue">Payload</span>
              </h2>
              <div className={cn(
                "grid gap-4 md:gap-6",
                isPS5 ? "grid-cols-2 lg:grid-cols-3 xl:grid-cols-4" : "grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4"
              )}>
                {loadingPayloads ? (
                  Array.from({ length: 4 }).map((_, i) => (
                    <div key={i} className="glass-card p-6 rounded-ps-xl flex flex-col space-y-2 border-white/5 animate-pulse">
                      <div className="h-7 w-40 bg-white/5 rounded-lg" />
                      <div className="h-3 w-20 bg-white/5 rounded-md opacity-50" />
                    </div>
                  ))
                ) : payloads.length === 0 ? (
                  <div className="col-span-full py-20 border-2 border-dashed border-white/5 rounded-ps-xl flex flex-col items-center justify-center space-y-6 bg-white/[0.01]">
                    <Package className="w-16 h-16 text-white/10" />
                    <div className="text-center">
                      <p className="text-white font-extrabold tracking-tight text-2xl">Empty Library</p>
                      <p className="text-zinc-500 font-medium">Add payloads from the Cloud Hub to get started.</p>
                    </div>
                    <button onClick={() => setView('storage')} className="px-8 py-3 bg-ps-blue text-white rounded-xl font-bold tracking-tight shadow-xl shadow-ps-blue/20">Open Repository</button>
                  </div>
                ) : (
                  payloads.map((p, i) => (
                    <PayloadButton
                      key={i}
                      path={p}
                      onClick={() => loadPayload(p)}
                      isLoading={loading && activeLoadingName === p.split('/').pop().replace(/\.(elf|bin|lua)$/i, '').replace(/_/g, ' ')}
                    />
                  ))
                )}
              </div>
            </div>
          )}

          {view === 'storage' && (
            <StorageHub payloads={payloads} onInstall={handleInstall} onDelete={handleDelete} onUpload={handleUpload} ip={ip} />
          )}

          {view === 'autoload' && (
            <AutoloadView payloads={payloads} config={config} onSaveConfig={handleSaveConfig} onToast={addToast} />
          )}

          {view === 'settings' && (
            <SettingsView config={config} onSaveConfig={handleSaveConfig} isPS5={isPS5} logs={logs} setLogs={setLogs} />
          )}
          {view === 'donate' && <DonateView />}
        </main>
      </div>

      {loading && (
        <div className="fixed inset-0 bg-ps-black/95 z-[9999] flex flex-col items-center justify-center space-y-12">
          <div className="ps5-robust-spinner" />
          <div className="text-center">
            <h4 className="text-4xl font-extrabold text-white tracking-tight mb-4 uppercase italic">{activeLoadingName || "Engaging Core"}</h4>
            <p className="label-caps !text-ps-blue tracking-[0.3em] font-black">LAUNCHING PAYLOAD...</p>
          </div>
        </div>
      )}
    </div>
  )
}

export default App
