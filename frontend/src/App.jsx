import React, { useState, useEffect, useRef, useMemo } from 'react'
import { QRCodeSVG } from 'qrcode.react'
import { 
  Play, 
  Settings, 
  Terminal, 
  CloudDownload, 
  Upload, 
  Trash2, 
  Cpu, 
  Activity,
  ArrowRight,
  ShieldCheck,
  RefreshCw,
  LayoutDashboard,
  Database,
  ArrowLeft,
  CheckCircle2,
  Loader2,
  AlertTriangle,
  Package,
  Power,
  Zap,
  Info,
  ChevronUp,
  ChevronDown
} from 'lucide-react'
import { clsx } from 'clsx'
import { twMerge } from 'tailwind-merge'
import './App.css'

// --- Utilities ---
function cn(...inputs) {
  return twMerge(clsx(inputs))
}

const parsePayloadName = (path) => {
  if (!path) return { displayName: '', version: null };
  if (path.startsWith('!')) {
    const ms = parseInt(path.substring(1));
    return { displayName: `Delay (${ms/1000}s)`, version: null, isDelay: true };
  }
  
  let name = path.split('/').pop().replace(/\.(elf|bin|lua)$/i, '');
  
  // Try to find version pattern like _v1.0 or -v1.0 or _1.0
  const versionMatch = name.match(/[_-](v?\d+[\d.a-z-]+)/i);
  let version = null;
  
  if (versionMatch) {
    version = versionMatch[1];
    name = name.replace(versionMatch[0], '');
  }
  
  return {
    displayName: name.replace(/_/g, ' ').replace(/-/g, ' '),
    version: version,
    isDelay: false
  };
};

const PayloadName = ({ path, className, versionClassName }) => {
  const { displayName, version, isDelay } = parsePayloadName(path);
  return (
    <div className={cn("flex items-center space-x-3", className)}>
      {isDelay && <Zap className="w-4 h-4 text-ps-blue" />}
      <span className="font-bold uppercase tracking-tight">{displayName}</span>
      {version && (
        <span className={cn("px-2 py-0.5 bg-white/10 text-ps-blue text-[10px] font-black uppercase rounded-md border border-white/5", versionClassName)}>
          {version}
        </span>
      )}
    </div>
  );
};

const PAYLOAD_EXPLORER_URL = 'https://itsplk.github.io/ps5_payloads/ps5_payloads.json'

// --- Custom Hook for Gamepad Navigation ---
function useGamepadNavigation(view, setView) {
  useEffect(() => {
    const handleKeyDown = (e) => {
      // Basic d-pad navigation support can be added here if needed
      // For now we rely on standard browser focus cycling
    }
    window.addEventListener('keydown', handleKeyDown)
    return () => window.removeEventListener('keydown', handleKeyDown)
  }, [view])
}

// --- Components ---

const Toast = ({ message, type = 'success', onClose }) => {
  useEffect(() => {
    const timer = setTimeout(onClose, 3000)
    return () => clearTimeout(timer)
  }, [onClose])

  return (
    <div className={cn(
      "fixed top-8 right-8 z-[1000] flex items-center space-x-4 p-5 rounded-2xl border shadow-2xl animate-in slide-in-from-right duration-300 pointer-events-auto",
      type === 'success' ? "bg-emerald-950/90 border-emerald-500/50 text-emerald-400" : "bg-red-950/90 border-red-500/50 text-red-400"
    )}>
      {type === 'success' ? <CheckCircle2 className="w-6 h-6" /> : <AlertTriangle className="w-6 h-6" />}
      <span className="font-bold uppercase tracking-tight">{message}</span>
    </div>
  )
}

const Modal = ({ show, title, children, onClose, footer }) => {
  if (!show) return null
  return (
    <div className="fixed inset-0 z-[1000] flex items-center justify-center p-8 animate-in fade-in duration-200">
      <div className="absolute inset-0 bg-black/80 backdrop-blur-md" onClick={onClose} />
      <div className="relative glass-panel w-full max-w-xl p-10 rounded-ps-3xl border-white/10 shadow-[0_0_100px_rgba(0,0,0,0.5)] animate-in zoom-in duration-300">
        <h3 className="text-3xl font-black text-white uppercase italic tracking-tighter mb-8">{title}</h3>
        <div className="text-zinc-300 mb-10 text-xl font-medium leading-relaxed">
          {children}
        </div>
        {footer && <div className="flex space-x-4">{footer}</div>}
      </div>
    </div>
  )
}

const NavButton = ({ active, onClick, icon: Icon, children }) => (
  <button 
    onClick={onClick}
    className={cn(
      "flex items-center space-x-3 px-6 py-3 rounded-2xl font-bold transition-all border",
      active ? "bg-ps-blue border-ps-blue text-white shadow-[0_0_20px_rgba(0,112,209,0.3)]" : "bg-transparent border-transparent text-zinc-400 hover:text-white hover:bg-white/5"
    )}
  >
    <Icon className="w-5 h-5" />
    <span className="uppercase tracking-tighter text-sm">{children}</span>
  </button>
)

const PayloadButton = ({ path, onClick, isLoading }) => {
  const name = path.split('/').pop().replace(/\.(elf|bin|lua)$/i, '').replace(/_/g, ' ')
  return (
    <button 
      onClick={onClick}
      disabled={isLoading}
      className="group glass-card p-6 rounded-ps-xl flex items-center justify-between border-white/5 hover:border-ps-blue hover:bg-ps-blue/5 transition-all text-left"
    >
      <div className="flex items-center space-x-4">
        <div className={cn(
          "p-3 rounded-xl transition-all",
          isLoading ? "bg-ps-blue text-white" : "bg-white/5 group-hover:bg-ps-blue group-hover:text-white"
        )}>
          {isLoading ? <Loader2 className="w-6 h-6 animate-spin" /> : <Play className="w-6 h-6" />}
        </div>
        <PayloadName path={path} className="text-white text-lg" />
      </div>
      <ArrowRight className="w-5 h-5 text-zinc-600 group-hover:text-ps-blue transition-all group-hover:translate-x-1" />
    </button>
  )
}

const AutoloadOverlay = ({ status, onCancel, onFinish }) => {
  const isCountdown = status.remaining > 0;
  const isExecuting = status.remaining === 0 && status.current !== 'DONE';
  const isDone = status.current === 'DONE';
  const payloadList = status.list ? status.list.split(',') : [];
  const listRef = useRef(null);
  const progress = status.total > 0 ? (status.done / status.total) : 0;

  useEffect(() => {
    if (listRef.current) {
      const activeItem = listRef.current.querySelector('[data-active="true"]');
      if (activeItem) {
        activeItem.scrollIntoView({ behavior: 'smooth', block: 'center' });
      }
    }
  }, [status.done]);

  return (
    <div className="fixed inset-0 bg-black/98 z-[9999] flex flex-col items-center justify-center p-8">
      <div className="relative text-center space-y-10 max-w-2xl w-full">
        {/* Conflict Warning (Only during active phases) */}
        {!isDone && (payloadList.some(p => p.toLowerCase().includes('etahen')) && 
          payloadList.some(p => p.toLowerCase().includes('kstuff'))) && (
          <div className="p-4 bg-amber-500/10 border border-amber-500/50 rounded-2xl flex items-center justify-center space-x-3 text-amber-500 animate-in fade-in">
            <AlertTriangle className="w-5 h-5" />
            <span className="font-bold uppercase tracking-tight text-xs">Conflict: etaHEN + KStuff active</span>
          </div>
        )}

        {/* Stable Status Header - Fixed height prevents jumping */}
        <div className="h-[320px] flex flex-col items-center justify-center">
          {isCountdown && (
            <div className="space-y-6 animate-in fade-in zoom-in duration-300">
              <p className="text-ps-blue font-black tracking-[0.4em] uppercase text-lg">Autoloading Sequence</p>
              <div className="relative h-48 w-48 mx-auto flex items-center justify-center">
                <svg className="absolute inset-0 w-full h-full -rotate-90">
                    <circle cx="96" cy="96" r="80" fill="none" stroke="currentColor" strokeWidth="6" className="text-white/5" />
                    <circle
                        cx="96" cy="96" r="80"
                        fill="none" stroke="currentColor" strokeWidth="6"
                        strokeDasharray="502"
                        strokeDashoffset={502 - (502 * (status.remaining / 5))}
                        className="text-ps-blue transition-all duration-1000 ease-linear"
                    />
                </svg>
                <span className="text-7xl font-black text-white tabular-nums leading-none">
                    {status.remaining}
                </span>
              </div>
            </div>
          )}

          {isExecuting && (
            <div className="space-y-6 animate-in fade-in zoom-in duration-300">
              <p className="text-ps-blue font-black tracking-[0.4em] uppercase text-lg">Executing Payloads</p>
              <div className="relative h-48 w-48 mx-auto flex items-center justify-center">
                <svg className="absolute inset-0 w-full h-full -rotate-90">
                    <circle cx="96" cy="96" r="80" fill="none" stroke="currentColor" strokeWidth="6" className="text-white/5" />
                    <circle
                        cx="96" cy="96" r="80"
                        fill="none" stroke="currentColor" strokeWidth="6"
                        strokeDasharray="502"
                        strokeDashoffset={502 - (502 * progress)}
                        className="text-ps-blue transition-all duration-500 ease-out"
                    />
                </svg>
                <span className="text-5xl font-black text-white tabular-nums leading-none">
                    {Math.round(progress * 100)}%
                </span>
              </div>
            </div>
          )}

          {isDone && (
              <div className="flex flex-col items-center space-y-6 animate-in zoom-in duration-500">
                  <div className="bg-emerald-500 text-white p-8 rounded-full shadow-[0_0_60px_rgba(16,185,129,0.5)]">
                      <CheckCircle2 className="w-16 h-16" />
                  </div>
                  <div className="text-center">
                      <h2 className="text-5xl font-black text-white uppercase tracking-tighter">Sequence Complete</h2>
                      <p className="text-zinc-500 font-bold uppercase text-sm tracking-[0.2em] mt-2">All payloads loaded successfully</p>
                  </div>
              </div>
          )}
        </div>

        {/* Payload Checklist - Fixed Height with Autoscroll */}
        <div 
          ref={listRef}
          className="w-full space-y-4 h-[350px] overflow-y-auto custom-scrollbar p-6 bg-white/5 rounded-3xl border border-white/10 shadow-inner scroll-smooth"
        >
            <div className="flex items-center justify-between mb-4 px-2 sticky top-0 bg-black/80 backdrop-blur-md py-2 z-10 rounded-xl">
                <h3 className="label-caps !text-zinc-500 text-xs">Payload Checklist</h3>
                <span className="text-zinc-400 font-bold text-sm">
                  {isDone ? status.total : status.done} / {status.total}
                </span>
            </div>
            
            <div className="space-y-2">
                {payloadList.map((name, i) => {
                    const active = !isDone && isExecuting && i === status.done;
                    const done = isDone || i < status.done;
                    return (
                        <div 
                          key={i} 
                          data-active={active}
                          className={`flex items-center justify-between p-4 rounded-2xl border transition-all duration-500 ${
                            active ? 'bg-ps-blue/20 border-ps-blue shadow-[0_0_30px_rgba(0,149,255,0.1)] scale-105 z-10' : 
                            done ? 'bg-emerald-500/5 border-emerald-500/20 opacity-90' : 'bg-white/10 border-white/10 opacity-50'
                        }`}>
                            <div className="flex items-center space-x-4">
                                {done ? <CheckCircle2 className="w-5 h-5 text-emerald-500" /> : 
                                 active ? <Loader2 className="w-5 h-5 text-ps-blue animate-spin" /> : 
                                 <div className="w-5 h-5 rounded-full border-2 border-white/10" />}
                                <PayloadName 
                                  path={name} 
                                  className={active ? 'text-white' : 'text-zinc-300'} 
                                  versionClassName={active ? 'bg-ps-blue text-white border-transparent' : ''}
                                />
                            </div>
                            {done && (
                                <span className="text-emerald-500 text-[10px] font-black uppercase tracking-widest">
                                    Done
                                </span>
                            )}
                        </div>
                    )
                })}
            </div>
        </div>

        <div className="pt-4 w-full">
            {isDone ? (
                <button 
                    onClick={onFinish}
                    className="w-full py-8 bg-ps-blue text-white text-3xl font-black uppercase rounded-3xl hover:bg-white hover:text-ps-blue transition-all transform active:scale-95 shadow-[0_0_50px_rgba(0,149,255,0.3)]"
                >
                    Return to Dashboard
                </button>
            ) : isCountdown ? (
                <button 
                    onClick={onCancel}
                    autoFocus
                    className="w-full py-8 bg-white/10 text-white border border-white/10 text-3xl font-black uppercase rounded-3xl hover:bg-red-600 hover:border-red-600 transition-all transform active:scale-95"
                >
                    Abort Sequence
                </button>
            ) : (
                /* No actions during execution */
                <div className="h-[92px]" /> 
            )}
        </div>
      </div>
    </div>
  )
}

const StorageHub = ({ payloads, onInstall, onDelete, onUpload }) => {
  const [subView, setSubView] = useState('menu')
  const [remotePayloads, setRemotePayloads] = useState([])
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState(false)

  const fetchRemote = async () => {
    setLoading(true)
    setError(false)
    try {
      const res = await fetch(PAYLOAD_EXPLORER_URL)
      if (!res.ok) throw new Error()
      const data = await res.json()
      if (Array.isArray(data)) setRemotePayloads(data)
    } catch (e) {
      setError(true)
    } finally {
      setLoading(false)
    }
  }

  useEffect(() => {
    if (subView === 'repo' && remotePayloads.length === 0) {
      fetchRemote()
    }
  }, [subView])

  const localFilenames = useMemo(() => payloads.map(p => p.split('/').pop()), [payloads])
  const internalPayloads = payloads.filter(p => !p.includes('/mnt/usb'))

  const getBaseName = (filename) => {
    if (!filename) return '';
    const clean = filename.replace(/\.(elf|bin|lua)$/i, '');
    const versionMatch = clean.match(/[_-](v?\d+[\d.a-z-]+)/i);
    return versionMatch ? clean.replace(versionMatch[0], '') : clean;
  }

  const remoteStatus = useMemo(() => {
    if (!Array.isArray(remotePayloads)) return []
    return remotePayloads.map(p => {
      const isInstalled = p.filename ? localFilenames.includes(p.filename) : false
      const baseName = getBaseName(p.filename)
      const installedVersion = localFilenames.find(f => getBaseName(f) === baseName)
      const isUpdate = !isInstalled && !!installedVersion
      
      return { ...p, isInstalled, isUpdate, installedFilename: installedVersion }
    }).sort((a, b) => {
      if (a.isUpdate && !b.isUpdate) return -1
      if (!a.isUpdate && b.isUpdate) return 1
      if (a.isInstalled && !b.isInstalled) return 1
      if (!a.isInstalled && b.isInstalled) return -1
      return 0
    })
  }, [remotePayloads, localFilenames])

  const updateCount = useMemo(() => {
    if (!Array.isArray(remotePayloads) || remotePayloads.length === 0) return 0;
    return remotePayloads.filter(p => {
       const isInstalled = p.filename ? localFilenames.includes(p.filename) : false
       const baseName = getBaseName(p.filename)
       const installedVersion = localFilenames.find(f => getBaseName(f) === baseName)
       return !isInstalled && !!installedVersion
    }).length
  }, [remotePayloads, localFilenames])

  if (subView === 'repo') {
    return (
      <div className="space-y-8 animate-fade-in">
        <div className="flex items-center space-x-6">
          <button onClick={() => setSubView('menu')} className="p-3 rounded-xl bg-white/5 hover:bg-white/10 border border-white/10">
            <ArrowLeft className="w-6 h-6" />
          </button>
          <h2 className="text-4xl font-black text-white uppercase italic tracking-tighter">
            Cloud <span className="text-ps-blue">Repository</span>
          </h2>
        </div>

        {loading ? (
           <div className="flex flex-col items-center justify-center py-32 space-y-6">
              <Loader2 className="w-16 h-16 text-ps-blue animate-spin" />
              <p className="label-caps animate-pulse">Connecting to Repository...</p>
           </div>
        ) : error ? (
           <div className="flex flex-col items-center justify-center py-32 space-y-6 text-center">
              <AlertTriangle className="w-16 h-16 text-red-500" />
              <div className="space-y-2">
                 <p className="text-xl font-bold text-white uppercase">Connection Failed</p>
                 <p className="text-zinc-500">Could not reach the cloud repository. Please check your internet connection.</p>
              </div>
              <button 
                onClick={fetchRemote} 
                className="px-8 py-3 bg-white/5 border border-white/10 hover:bg-white/10 text-white rounded-xl font-bold uppercase transition-all"
              >
                Retry Connection
              </button>
           </div>
        ) : (
          <div className="grid grid-cols-1 gap-6 max-h-[70vh] overflow-y-auto p-2 custom-scrollbar">
            {remoteStatus.map((p, i) => (
            <div key={i} className={cn(
              "glass-card p-6 rounded-ps-2xl flex items-center justify-between border-white/10 transition-all",
              p.isInstalled && "opacity-50 grayscale bg-black/40"
            )}>
              <div className="space-y-2">
                <div className="flex items-center space-x-4">
                   <span className="font-bold text-white uppercase text-xl tracking-tight">{p.filename}</span>
                   {p.isUpdate && (
                     <span className="text-[10px] bg-emerald-500 text-white px-2 py-1 rounded font-black uppercase tracking-widest">Update Available</span>
                   )}
                   {p.isInstalled && (
                     <span className="text-[10px] bg-zinc-700 text-zinc-300 px-2 py-1 rounded font-black uppercase tracking-widest">Installed</span>
                   )}
                </div>
                <p className="text-md text-zinc-300 font-medium max-w-2xl">{p.description}</p>
              </div>
              
              {!p.isInstalled && (
                <button 
                  onClick={() => onInstall(p, p.installedFilename)} 
                  className={cn(
                    "flex items-center space-x-3 px-6 py-4 rounded-xl font-bold transition-all shadow-xl",
                    p.isUpdate ? "bg-emerald-600 hover:bg-emerald-500 text-white" : "bg-ps-blue hover:bg-ps-blue/80 text-white"
                  )}
                >
                  <CloudDownload className="w-6 h-6" />
                  <span>{p.isUpdate ? "Update" : "Install"}</span>
                </button>
              )}
            </div>
          ))}
          </div>
        )}
      </div>
    )
  }

  if (subView === 'remove') {
    return (
      <div className="space-y-8 animate-fade-in">
        <div className="flex items-center space-x-6">
          <button onClick={() => setSubView('menu')} className="p-3 rounded-xl bg-white/5 hover:bg-white/10 border border-white/10">
            <ArrowLeft className="w-6 h-6" />
          </button>
          <h2 className="text-4xl font-black text-white uppercase italic tracking-tighter">
            Remove <span className="text-red-500">Payload</span>
          </h2>
        </div>

        <div className="grid grid-cols-1 gap-4">
          {internalPayloads.map((path, i) => {
            const fileName = path.split('/').pop()
            return (
              <div key={i} className="flex items-center justify-between p-6 glass-card rounded-ps-xl border-white/10 hover:bg-white/[0.02]">
                <div>
                   <p className="font-bold text-white uppercase text-xl tracking-tight">{fileName}</p>
                   <p className="text-sm font-mono text-zinc-400">{path}</p>
                </div>
                <button 
                  onClick={() => onDelete(fileName)}
                  className="p-4 rounded-xl bg-red-950/40 text-red-500 border border-red-500/20 hover:bg-red-500 hover:text-white transition-all shadow-lg"
                >
                  <Trash2 className="w-6 h-6" />
                </button>
              </div>
            )
          })}
        </div>
      </div>
    )
  }



  return (
    <div className="space-y-12 animate-fade-in">
      <h2 className="text-4xl font-black text-white uppercase italic tracking-tighter">
        Payload <span className="text-ps-blue">Management</span>
      </h2>
      
      <div className="grid grid-cols-1 md:grid-cols-2 gap-8">
        <button 
          onClick={() => setSubView('repo')}
          className="relative glass-card p-10 rounded-ps-2xl flex flex-col items-center justify-center space-y-4 border-white/10 hover:border-ps-blue group"
        >
          {updateCount > 0 && (
            <span className="absolute top-6 right-6 bg-emerald-500 text-white px-3 py-1 rounded-full text-[10px] font-black uppercase tracking-widest shadow-lg z-10">
              {updateCount} Update{updateCount > 1 ? 's' : ''}
            </span>
          )}
          <div className="p-6 rounded-3xl bg-ps-blue/10 group-hover:bg-ps-blue group-hover:text-white transition-all">
            <CloudDownload className="w-12 h-12" />
          </div>
          <span className="text-xl font-black uppercase tracking-tighter">Get Payloads from Repository</span>
        </button>

        <button 
          onClick={() => setSubView('remove')}
          className="glass-card p-10 rounded-ps-2xl flex flex-col items-center justify-center space-y-4 border-white/10 hover:border-red-500 group"
        >
          <div className="p-6 rounded-3xl bg-red-500/10 group-hover:bg-red-500 group-hover:text-white transition-all">
            <Trash2 className="w-12 h-12" />
          </div>
          <span className="text-xl font-black uppercase tracking-tighter">Remove Payload</span>
        </button>
      </div>

      <div className="glass-card p-10 rounded-ps-2xl flex items-center space-x-12 border-white/10 bg-white/[0.02]">
        <div className="bg-white p-6 rounded-3xl shadow-2xl shrink-0">
          <QRCodeSVG value={`http://${window.location.host}`} size={180} level="M" />
        </div>
        <div className="flex-1 space-y-6">
          <h4 className="label-caps mb-3 text-lg">Network Control</h4>
          <p className="text-md text-zinc-300 leading-relaxed italic font-medium">
            Scan OR view <code>http://{window.location.host}</code> in your desktop browser for easy management and file transfers.
          </p>
          <label className="inline-flex items-center space-x-3 px-8 py-4 bg-ps-blue hover:bg-ps-blue/80 text-white rounded-2xl font-bold cursor-pointer transition-all shadow-xl">
             <Upload className="w-6 h-6" />
             <span>Upload Local ELF</span>
             <input type="file" className="hidden" onChange={onUpload} accept=".elf,.bin,.lua" />
          </label>
        </div>
      </div>
    </div>
  )
}

const AutoloadView = ({ payloads, config, onSaveConfig, onToast }) => {
  const [enabled, setEnabled] = useState(false)
  const [autoloadList, setAutoloadList] = useState([])
  const [showDelayModal, setShowDelayModal] = useState(false)
  const [customDelay, setCustomDelay] = useState('')

  useEffect(() => {
    if (config) {
      setEnabled(config.AUTOLOAD_ENABLED === true || config.AUTOLOAD_ENABLED === "true")
      setAutoloadList(config.AUTOLOAD_LIST ? config.AUTOLOAD_LIST.split(',').filter(x => x) : [])
    }
  }, [config])

  const internalPayloads = payloads.filter(p => !p.includes('/mnt/usb')).map(p => p.split('/').pop())
  const availablePayloads = internalPayloads.filter(p => !autoloadList.includes(p))

  const handleToggle = (val) => {
    setEnabled(val)
    onSaveConfig({ AUTOLOAD_ENABLED: val, AUTOLOAD_LIST: autoloadList.join(',') })
  }

  const moveUp = (index) => {
    if (index === 0) return
    const newList = [...autoloadList]
    ;[newList[index - 1], newList[index]] = [newList[index], newList[index - 1]]
    setAutoloadList(newList)
  }

  const moveDown = (index) => {
    if (index === autoloadList.length - 1) return
    const newList = [...autoloadList]
    ;[newList[index + 1], newList[index]] = [newList[index], newList[index + 1]]
    setAutoloadList(newList)
  }

  const hasEtaHen = autoloadList.some(p => p.toLowerCase().includes('etahen'))
  const hasKstuff = autoloadList.some(p => p.toLowerCase().includes('kstuff'))

  const handleSave = () => {
    // Automatically disable if list is empty
    const shouldEnable = autoloadList.length > 0 && enabled
    if (autoloadList.length === 0) setEnabled(false)

    // Map DELAY to !1000 for backend
    const finalList = autoloadList.map(p => p === 'DELAY' ? '!1000' : p)
    
    onSaveConfig({
      AUTOLOAD_ENABLED: shouldEnable,
      AUTOLOAD_LIST: finalList.join(',')
    })
  }

  const addDelay = (ms) => {
    setAutoloadList([...autoloadList, `!${ms}`])
    setShowDelayModal(false)
  }

  const addPayload = (p) => {
    const isKstuff = p.toLowerCase().includes('kstuff');
    if (isKstuff) {
      const existing = autoloadList.find(x => x.toLowerCase().includes('kstuff'));
      if (existing) {
        onToast(`Conflict: Multiple KStuff payloads detected. Replace the existing one first.`, 'error');
        return;
      }
    }
    setAutoloadList([...autoloadList, p]);
  }

  if (!enabled) {
    return (
      <div className="flex flex-col items-center justify-center space-y-12 py-20 animate-fade-in text-center max-w-4xl mx-auto">
        <div className="p-12 bg-ps-blue/10 rounded-[3.5rem] border border-ps-blue/20">
           <RefreshCw className="w-24 h-24 text-ps-blue" />
        </div>
        <div className="space-y-6">
           <h2 className="text-6xl font-black text-white uppercase italic tracking-tighter">
             Autoload <span className="text-ps-blue">Sequence</span>
           </h2>
           <p className="text-2xl text-zinc-400 font-medium leading-relaxed italic">
             Chain multiple payloads to be executed automatically every time Next Menu starts.
           </p>
        </div>
        <button 
          onClick={() => handleToggle(true)}
          className="px-16 py-8 bg-ps-blue text-white text-3xl font-black uppercase rounded-[2rem] hover:bg-ps-blue/80 transition-all transform active:scale-95 shadow-[0_0_50px_rgba(0,149,255,0.4)]"
        >
          Enable Autoload
        </button>
      </div>
    )
  }

  return (
    <div className="space-y-12 animate-fade-in">
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-12 items-start">
        <div className="space-y-8">
          <div className="flex items-center justify-between">
            <h3 className="label-caps !text-white !opacity-100 text-xl tracking-widest">Available Payloads</h3>
            <button 
              onClick={() => handleToggle(false)}
              className="px-6 py-2 rounded-xl font-black uppercase italic tracking-tighter bg-red-600/10 text-red-500 border border-red-500/30 hover:bg-red-600 hover:text-white transition-all shadow-lg text-xs"
            >
              Disable Autoload
            </button>
          </div>
          <div className="grid grid-cols-1 gap-4">
             {availablePayloads.map(p => {
                const isKstuff = p.toLowerCase().includes('kstuff');
                const hasKstuff = autoloadList.some(x => x.toLowerCase().includes('kstuff'));
                const isBlocked = isKstuff && hasKstuff;

                return (
                  <button 
                    key={p} 
                    onClick={() => !isBlocked && addPayload(p)}
                    disabled={isBlocked}
                    className={cn(
                      "flex items-center justify-between p-6 glass-card rounded-2xl border-white/20 transition-all",
                      isBlocked ? "opacity-40 cursor-not-allowed" : "bg-white/[0.03] hover:border-ps-blue group"
                    )}
                  >
                    <PayloadName path={p} className={cn("text-xl", isBlocked ? "text-zinc-500" : "text-white")} />
                    <ArrowRight className={cn("w-6 h-6 transition-all", isBlocked ? "text-zinc-800" : "text-zinc-500 group-hover:text-ps-blue group-hover:translate-x-2")} />
                  </button>
                )
             })}
             
             {/* Extra Actions */}
             <div className="pt-4 border-t border-white/10 mt-4">
                <button 
                  onClick={() => setShowDelayModal(true)}
                  className="w-full flex items-center justify-between p-6 bg-white/[0.03] rounded-2xl border border-dashed border-white/20 hover:border-ps-blue group transition-all"
                >
                  <div className="flex items-center space-x-4">
                    <Zap className="w-6 h-6 text-ps-blue" />
                    <span className="font-bold text-white uppercase tracking-tight text-xl">Add Delay</span>
                  </div>
                  <ArrowRight className="w-6 h-6 text-zinc-500 group-hover:text-ps-blue group-hover:translate-x-2 transition-all" />
                </button>
             </div>
          </div>
        </div>

        <div className="space-y-8">
          <h2 className="text-4xl font-black text-white uppercase italic tracking-tighter">
            Autoload <span className="text-ps-blue">Sequence</span>
          </h2>
          <div className="glass-panel p-6 rounded-ps-3xl border-white/10 space-y-4 min-h-[400px] flex flex-col">
             {hasEtaHen && hasKstuff && (
               <div className="p-4 bg-amber-500/10 border border-amber-500/50 rounded-2xl flex items-center space-x-4 text-amber-500 mb-2">
                 <AlertTriangle className="w-6 h-6" />
                 <span className="font-bold uppercase tracking-tight text-xs">Warning: etaHEN and KStuff are both active. This may cause instability.</span>
               </div>
             )}
             
             {autoloadList.map((p, i) => (
               <div key={`${p}-${i}`} className="flex items-center justify-between p-4 bg-white/5 rounded-2xl border border-white/10">
                  <div className="flex items-center space-x-4">
                     <span className="text-ps-blue font-black italic">{i + 1}</span>
                     <PayloadName path={p} className="text-white" />
                  </div>
                  <div className="flex items-center space-x-3">
                    <button 
                      onClick={() => moveUp(i)} 
                      disabled={i === 0}
                      className="p-3 bg-white/10 text-zinc-400 hover:bg-ps-blue hover:text-white rounded-xl transition-all disabled:opacity-5 shadow-lg"
                    >
                       <ChevronUp className="w-5 h-5" />
                    </button>
                    <button 
                      onClick={() => moveDown(i)} 
                      disabled={i === autoloadList.length - 1}
                      className="p-3 bg-white/10 text-zinc-400 hover:bg-ps-blue hover:text-white rounded-xl transition-all disabled:opacity-5 shadow-lg"
                    >
                       <ChevronDown className="w-5 h-5" />
                    </button>
                    <button 
                      onClick={() => setAutoloadList(autoloadList.filter((_, idx) => idx !== i))} 
                      className="p-3 bg-white/10 text-zinc-400 hover:bg-red-600 hover:text-white rounded-xl transition-all shadow-lg"
                    >
                       <Trash2 className="w-5 h-5" />
                    </button>
                  </div>
               </div>
             ))}
             {autoloadList.length === 0 && (
               <div className="flex-1 flex flex-col items-center justify-center opacity-10 italic">
                  <RefreshCw className="w-16 h-16 mb-4" />
                  <p className="text-2xl font-black uppercase">Sequence Empty</p>
               </div>
             )}
             <button onClick={handleSave} className="w-full py-5 bg-ps-blue hover:bg-ps-blue/80 text-white rounded-2xl font-black uppercase italic tracking-tighter text-xl transition-all shadow-2xl mt-auto">
                Save Sequence
             </button>
          </div>
        </div>
      </div>
      
      <Modal 
        show={showDelayModal} 
        title="Configure Delay" 
        onClose={() => setShowDelayModal(false)}
        footer={
          <button 
            onClick={() => setShowDelayModal(false)}
            className="w-full py-4 bg-white/5 hover:bg-white/10 text-white rounded-2xl font-bold uppercase tracking-tight transition-all"
          >
            Cancel
          </button>
        }
      >
        <div className="space-y-8">
           <div className="grid grid-cols-3 gap-4">
              {[1, 3, 5].map(s => (
                <button 
                  key={s}
                  onClick={() => addDelay(s * 1000)}
                  className="py-6 bg-ps-blue/20 hover:bg-ps-blue border border-ps-blue/30 text-white rounded-2xl font-black text-2xl transition-all shadow-lg"
                >
                  {s}s
                </button>
              ))}
           </div>
           
           <div className="space-y-4">
              <p className="label-caps !text-zinc-500">Custom Delay (milliseconds)</p>
              <div className="flex space-x-4">
                 <input 
                    type="number" 
                    placeholder="e.g. 2500"
                    value={customDelay}
                    onChange={(e) => setCustomDelay(e.target.value)}
                    className="flex-1 bg-white/5 border border-white/10 rounded-2xl p-5 text-white font-mono text-2xl focus:border-ps-blue outline-none transition-all"
                 />
                 <button 
                    onClick={() => customDelay && addDelay(parseInt(customDelay))}
                    className="px-10 bg-ps-blue text-white rounded-2xl font-black uppercase italic tracking-tighter text-xl shadow-2xl hover:bg-ps-blue/80 transition-all"
                 >
                    Add
                 </button>
              </div>
              <p className="text-sm text-zinc-500 italic">1000 milliseconds = 1 second. Useful for fine-tuning payload execution order.</p>
           </div>
        </div>
      </Modal>
    </div>
  )
}

const SettingsView = ({ ip, version }) => (
  <div className="flex flex-col items-center justify-center min-h-[60vh] text-center space-y-12 animate-fade-in">
     <div className="p-12 rounded-[3.5rem] bg-zinc-900 border border-white/20 text-ps-blue shadow-2xl">
        <CheckCircle2 className="w-24 h-24" />
     </div>
     <div className="space-y-4">
        <h3 className="text-6xl font-black text-white uppercase italic tracking-tighter">Next <span className="text-ps-blue">Menu</span></h3>
        <p className="text-xl text-zinc-400 font-medium max-w-xl mx-auto italic">
           Unified payload deployment system for modern PlayStation 5 environments.
        </p>
     </div>
     <div className="grid grid-cols-2 gap-6 w-full max-w-xl">
        <div className="p-6 bg-white/5 border border-white/10 rounded-3xl">
           <p className="label-caps !text-[11px] mb-2">Version</p>
           <p className="text-2xl font-black text-white">{version}</p>
        </div>
        <div className="p-6 bg-white/5 border border-white/10 rounded-3xl">
           <p className="label-caps !text-[11px] mb-2">Endpoint</p>
           <p className="text-2xl font-mono font-black text-ps-blue">{ip}</p>
        </div>
     </div>
  </div>
)

function App() {
  const [view, setView] = useState('dashboard')
  const [autoloadStatus, setAutoloadStatus] = useState(null)
  const [logs, setLogs] = useState([])
  const [payloads, setPayloads] = useState([])
  const [config, setConfig] = useState({})
  const [ip, setIp] = useState('0.0.0.0')
  const [version, setVersion] = useState('Loading...')
  const [loading, setLoading] = useState(false)
  const [activeLoadingName, setActiveLoadingName] = useState('')
  const [toasts, setToasts] = useState([])
  const [downloadModal, setDownloadModal] = useState({ show: false, name: '', progress: 0 })
  const [confirmModal, setConfirmModal] = useState({ show: false, title: '', message: '', onConfirm: null })
  
  const logEndRef = useRef(null)

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
        if (text.trim().startsWith('<!DOCTYPE')) return null
        return JSON.parse(text)
      } catch (e) { return null }
    } catch (e) { return null }
  }

  const refreshPayloads = async () => {
    const data = await api('/list_payloads')
    if (data?.payloads) setPayloads(data.payloads)
  }

  const refreshConfig = async () => {
    const data = await api('/get_config')
    if (data) setConfig(data)
  }

  const handleAbort = async () => {
    await fetch('/abort').catch(() => {})
    setAutoloadStatus(prev => prev ? { ...prev, remaining: -1 } : null)
    addToast("Sequence Aborted", "error")
  }

  const handleFinish = async () => {
    await fetch('/autoload_clear').catch(() => {})
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

  const deletePayload = (fileName) => {
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

  const handleFileUpload = async (e) => {
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

  const handleRemoteInstall = async (p, oldFilename = null) => {
    setDownloadModal({ show: true, name: p.filename, progress: 10 })
    try {
      const resp = await fetch(p.url)
      setDownloadModal(prev => ({ ...prev, progress: 40 }))
      const blob = await resp.blob()
      setDownloadModal(prev => ({ ...prev, progress: 70 }))
      
      const uploadResp = await fetch(`/manage:upload?filename=${encodeURIComponent(p.filename)}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: blob
      })
      
      if (uploadResp.ok) {
        setDownloadModal(prev => ({ ...prev, progress: 100 }))
        if (oldFilename) await fetch(`/manage:delete?filename=${encodeURIComponent(oldFilename)}`)
        addToast(`${p.filename} installed`)
        refreshPayloads()
      } else throw new Error()
    } catch (e) { addToast("Installation failed", "error") }
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
      addToast("Configuration saved")
      refreshConfig()
    } else addToast("Save failed", "error")
  }

  useEffect(() => {
    const init = async () => {
      const ipRes = await fetch('/getip').then(r => r.text()).catch(() => '0.0.0.0')
      setIp(ipRes.includes('<!DOCTYPE') ? '192.168.1.133' : ipRes)
      const verRes = await fetch('/version').then(r => r.text()).catch(() => '?')
      setVersion(verRes.includes('<!DOCTYPE') ? '1.0.0-dev' : verRes)
      refreshPayloads()
      refreshConfig()
    }
    init()

    let statusTimeout;
    const pollStatus = async () => {
      const status = await api('/autoload_status');
      if (status) {
        setAutoloadStatus(status);
        if (status.remaining >= 0 || status.current === 'DONE') {
          statusTimeout = setTimeout(pollStatus, 1000);
          return;
        }
      }
    };
    pollStatus();

    const eventSource = new EventSource('/events')
    eventSource.onmessage = (e) => {
      setLogs(prev => [...prev, e.data].slice(-100))
    }

    return () => { 
      clearTimeout(statusTimeout)
      eventSource.close()
    }
  }, [])

  useEffect(() => { logEndRef.current?.scrollIntoView({ behavior: 'auto' }) }, [logs])

  return (
    <div className="min-h-screen ps5-bg text-zinc-100 font-ps5 overflow-hidden flex flex-col">
      {/* Toast Container */}
      <div className="fixed top-0 right-0 p-8 z-[2000] space-y-4 pointer-events-none">
        {toasts.map(t => (
          <Toast key={t.id} message={t.message} type={t.type} onClose={() => removeToast(t.id)} />
        ))}
      </div>

      {/* Modals */}
      <Modal show={downloadModal.show} title="Processing Payload" onClose={() => {}}>
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
        <AutoloadOverlay status={autoloadStatus} onCancel={handleAbort} onFinish={handleFinish} />
      )}

      {/* Main Layout */}
      <div className="flex-1 overflow-auto custom-scrollbar relative">
        <nav className="sticky top-0 z-50 bg-black/40 backdrop-blur-xl border-b border-white/5 px-12 py-6 flex items-center justify-between shadow-2xl">
          <div className="flex items-center space-x-10">
            <div className="flex items-center space-x-4">
               <div className="p-3 bg-ps-blue rounded-2xl shadow-[0_0_30px_rgba(0,112,209,0.4)]">
                 <Cpu className="w-8 h-8 text-white" />
               </div>
               <span className="text-3xl font-black italic uppercase tracking-tighter text-white">Next <span className="text-ps-blue">Menu</span></span>
            </div>
            <div className="h-10 w-px bg-white/10" />
            <div className="flex items-center space-x-8">
               <NavButton active={view === 'dashboard'} onClick={() => setView('dashboard')} icon={LayoutDashboard}>Dashboard</NavButton>
               <NavButton active={view === 'storage'} onClick={() => setView('storage')} icon={Database}>Storage Hub</NavButton>
               <NavButton active={view === 'autoload'} onClick={() => setView('autoload')} icon={RefreshCw}>Autoload</NavButton>
               <NavButton active={view === 'settings'} onClick={() => setView('settings')} icon={Settings}>Info</NavButton>
            </div>
          </div>
          <div className="flex items-center space-x-6">
            <div className="flex flex-col items-end">
              <span className="text-[10px] label-caps opacity-40">System Node</span>
              <span className="text-ps-blue font-mono font-black text-lg tracking-tight">{ip}</span>
            </div>
          </div>
        </nav>

        <main className="p-16 max-w-[1800px] mx-auto">
          {view === 'dashboard' && (
            <div className="space-y-16">
              <div className="grid grid-cols-1 lg:grid-cols-2 gap-12">
                 <div className="space-y-8 animate-slide-up [animation-delay:100ms]">
                    <div className="flex items-center justify-between">
                       <h2 className="text-3xl font-black text-white uppercase italic tracking-tighter flex items-center space-x-3">
                         <Play className="w-8 h-8 text-ps-blue" />
                         <span>Ready <span className="text-ps-blue">Payloads</span></span>
                       </h2>
                       <button onClick={() => setView('storage')} className="flex items-center space-x-2 text-ps-blue hover:text-white transition-colors group">
                         <span className="label-caps !text-xs">Manage Storage</span>
                         <ArrowRight className="w-4 h-4 group-hover:translate-x-1 transition-transform" />
                       </button>
                    </div>
                    <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
                      {payloads.map((p, i) => (
                        <PayloadButton 
                          key={i} 
                          path={p} 
                          onClick={() => loadPayload(p)} 
                          isLoading={loading && activeLoadingName === p.split('/').pop().replace(/\.(elf|bin|lua)$/i, '').replace(/_/g, ' ')}
                        />
                      ))}
                      {payloads.length === 0 && (
                        <div className="col-span-2 py-12 px-8 border-2 border-dashed border-white/5 rounded-ps-xl flex flex-col items-center justify-center space-y-4 bg-white/[0.01]">
                           <Package className="w-12 h-12 text-white/10" />
                           <p className="text-zinc-500 font-bold uppercase tracking-tight text-lg">No Payloads Found</p>
                           <button onClick={() => setView('storage')} className="btn-primary py-3 px-8 text-sm">Add from Hub</button>
                        </div>
                      )}
                    </div>
                 </div>

                 <div className="space-y-8 animate-slide-up [animation-delay:200ms]">
                    <h2 className="text-3xl font-black text-white uppercase italic tracking-tighter flex items-center space-x-3">
                       <Terminal className="w-8 h-8 text-ps-blue" />
                       <span>Core <span className="text-ps-blue">Logs</span></span>
                    </h2>
                    <div className="glass-panel h-[600px] overflow-hidden flex flex-col rounded-ps-3xl border-white/10 shadow-2xl relative">
                       <div className="absolute top-0 inset-x-0 h-12 bg-gradient-to-b from-black/80 to-transparent z-10" />
                       <div className="flex-1 overflow-auto p-8 font-mono text-sm space-y-2 custom-scrollbar scroll-smooth">
                          {logs.map((log, i) => (
                            <div key={i} className="flex space-x-4 group">
                               <span className="text-zinc-600 select-none font-bold shrink-0">{i + 1}</span>
                               <span className="text-ps-blue/80 font-bold opacity-60">»</span>
                               <span className="text-zinc-300 break-all leading-relaxed group-hover:text-white transition-colors">{log}</span>
                            </div>
                          ))}
                          <div ref={logEndRef} />
                       </div>
                       <div className="absolute bottom-0 inset-x-0 h-12 bg-gradient-to-t from-black/80 to-transparent pointer-events-none" />
                    </div>
                 </div>
              </div>
            </div>
          )}

          {view === 'storage' && (
            <StorageHub 
              payloads={payloads} 
              onInstall={handleRemoteInstall}
              onDelete={deletePayload}
              onUpload={handleFileUpload}
            />
          )}

          {view === 'autoload' && (
            <AutoloadView 
              payloads={payloads} 
              config={config} 
              onSaveConfig={handleSaveConfig} 
              onToast={addToast}
            />
          )}

          {view === 'settings' && <SettingsView ip={ip} version={version} />}
        </main>
      </div>

      {loading && (
        <div className="fixed inset-0 bg-black/85 backdrop-blur-lg z-[9999] flex flex-col items-center justify-center space-y-8 animate-fade-in">
           <div className="relative">
              <div className="w-32 h-32 border-8 border-ps-blue/10 rounded-full" />
              <div className="absolute inset-0 w-32 h-32 border-8 border-ps-blue rounded-full border-t-transparent animate-spin shadow-[0_0_50px_rgba(0,149,255,0.5)]" />
           </div>
           <div className="text-center">
              <h4 className="text-4xl font-black italic text-white uppercase tracking-tighter mb-3">{activeLoadingName || "Engaging Core"}</h4>
              <p className="label-caps !text-[16px] animate-pulse text-ps-blue">LAUNCHING PAYLOAD. PLEASE WAIT...</p>
           </div>
        </div>
      )}
    </div>
  )
}

export default App
