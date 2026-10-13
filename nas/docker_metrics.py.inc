# Canonical Docker metric implementation; entry files supply stdlib imports,
# DOCKER_SOCKET and MAX_RESPONSE. Sync with python3 nas/sync_docker_metrics.py --write.
# Embedded at build time: root never imports code from a writable app directory.

def _metrics_get(identifier, suffix):
    if not re.fullmatch(r'[0-9a-f]{64}', identifier) or suffix not in ('json', 'stats?stream=false&one-shot=true'):
        raise ValueError('invalid read-only Docker metric request')
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as raw:
        raw.settimeout(0.3)
        raw.connect(DOCKER_SOCKET)
        request = ('GET /containers/%s/%s HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n' % (identifier, suffix)).encode('ascii')
        raw.sendall(request)
        with http.client.HTTPResponse(raw) as response:
            response.begin()
            if response.status != 200:
                raise RuntimeError('Docker metrics unavailable')
            body = response.read(MAX_RESPONSE+1)
    if len(body)>MAX_RESPONSE:
        raise ValueError('Docker metric response exceeds budget')
    result=json.loads(body)
    if not isinstance(result,dict):
        raise ValueError('invalid Docker metrics')
    return result

def _metric_value(value):
    return isinstance(value,(int,float)) and not isinstance(value,bool) and math.isfinite(value) and value>=0

class DockerMetrics:
    """One collector owns rotation, cached readings and CPU baselines.

    request reads a fixed Docker metric endpoint; clock is an optional wall clock.
    enrich updates panel rows in the same source order as raw_rows, before sorting.
    """
    def __init__(self, request, clock=None):
        self._request = request
        self._clock = clock
        self._cache = {}
        self._previous = {}
        self._cursor = 0

    def enrich(self, raw_rows, rows):
        """Rotate through every entity; bounded work per frame, explicit cache timestamp."""
        identities=[str(c.get('Id','')) for c in raw_rows]
        ids={i for i in identities if re.fullmatch(r'[0-9a-f]{64}',i)}
        self._cache={k:v for k,v in self._cache.items() if k in ids}
        self._previous={k:v for k,v in self._previous.items() if k in ids}
        eligible=sorted(ids)
        if eligible:
            identifier=eligible[self._cursor % len(eligible)]
            self._cursor+=1
            metrics={}
            # A failed inspection cannot publish old restart/resource values as new.
            try:
                info=self._request(identifier,'json')
                count=info.get('RestartCount')
                if isinstance(count,int) and not isinstance(count,bool) and count>=0:
                    metrics['restarts']=count
            except (OSError, ValueError, RuntimeError, http.client.HTTPException):
                pass
            container=next(c for c in raw_rows if c.get('Id')==identifier)
            if container.get('State')=='running':
                try:
                    stats=self._request(identifier,'stats?stream=false&one-shot=true')
                    cpu=stats.get('cpu_stats') or {}
                    usage=(cpu.get('cpu_usage') or {}).get('total_usage')
                    system=cpu.get('system_cpu_usage')
                    cores=cpu.get('online_cpus') or len((cpu.get('cpu_usage') or {}).get('percpu_usage') or [])
                    prior=self._previous.get(identifier)
                    if all(_metric_value(v) for v in (usage,system,cores)):
                        if prior and usage>=prior[0] and system>prior[1] and cores>0:
                            metrics['cpu_pct']=round((usage-prior[0])/(system-prior[1])*cores*100,2)
                        self._previous[identifier]=(usage,system)
                    memory=stats.get('memory_stats') or {}
                    used=memory.get('usage')
                    inactive=(memory.get('stats') or {}).get('total_inactive_file', (memory.get('stats') or {}).get('inactive_file',0))
                    if _metric_value(used) and _metric_value(inactive):
                        metrics['mem_mb']=round(max(0,used-inactive)/1024**2,2)
                except (OSError, ValueError, RuntimeError, http.client.HTTPException):
                    self._previous.pop(identifier,None)
            else:
                self._previous.pop(identifier,None)
            if metrics: metrics['metrics_ts']=int(self._clock() if self._clock else time.time())
            self._cache[identifier]=metrics
        # Rows are still in source order here. Sorting happens after enrichment.
        for original,row in zip(raw_rows,rows):
            identifier=str(original.get('Id',''))
            row.update(id=identifier,cpu_pct=None,mem_mb=None,restarts=None,metrics_ts=0)
            row.update(self._cache.get(identifier,{}))
