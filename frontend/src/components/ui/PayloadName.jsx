import React from 'react'
import { Zap, Usb } from 'lucide-react'
import { cn, parsePayloadName } from '../../utils/helpers'

const PayloadName = ({ path, className, versionClassName, stacked = false }) => {
  const { displayName, version, isDelay } = parsePayloadName(path);
  const isUsb = path?.startsWith('/mnt/usb');

  return (
    <div className={cn("flex min-w-0 flex-1", stacked ? "flex-col items-start" : "items-center space-x-3", className)}>
      <div className="flex items-center space-x-2 min-w-0">
        {isDelay && <Zap className="w-4 h-4 text-ps-blue shrink-0" />}
        {isUsb && <Usb className="w-5 h-5 text-ps-blue shrink-0 mr-1" />}
        <span className="font-extrabold truncate shrink leading-tight">{displayName}</span>
      </div>
      {version && (
        <span className={cn(
          stacked
            ? "text-[11px] font-bold tracking-wider text-ps-blue mt-1 opacity-90"
            : "text-[10px] px-2 py-0.5 bg-ps-blue/10 text-ps-blue font-bold rounded-md border border-ps-blue/20 shrink-0",
          versionClassName)}>
          {version}
        </span>
      )}
    </div>
  );
};

export default PayloadName
