# Kubernetes Lab — Workloads, Scheduling, and Resource Isolation

Local Kubernetes lab using **kind + Podman** on an M2 MacBook.

The labs follow one question from the view of a shared ML cluster: *why does (or doesn't) my workload end up as a running Pod?* They start with workload types (Job vs Deployment), then move through scheduling (requests, taints) to multi-team resource isolation (namespaces, quotas).

## Contents

- [Start the Cluster](#start-the-cluster)
- [Useful `kubectl` Concepts](#useful-kubectl-concepts)
- [Job Lab](#job-lab)
- [Deployment Lab](#deployment-lab)
- [Job vs Deployment](#job-vs-deployment)
- [Job Running vs Pod Running](#job-running-vs-pod-running)
- [Service Discovery and DNS Lab](#service-discovery-and-dns-lab)
- [CrashLoopBackOff](#crashloopbackoff)
- [Resource requests](#resource-requests)
- [Admission Failure vs Scheduling Failure](#admission-failure-vs-scheduling-failure)

## Start the Cluster

```bash
podman machine start
podman start mle-lab-control-plane

kubectl config use-context kind-mle-lab
kubectl get nodes
```

## Useful `kubectl` Concepts

```bash
kubectl get jobs       # Controller/workload state
kubectl get pods       # Execution state
kubectl get pods -w    # Watch Pod state changes
```

```text
Controller → Pod → Container → Process
```

A **Job/Deployment** (controller) manages desired state; a **Pod** is the execution unit, running containers, each a process.

### Resource relationships: everything centers on the Pod

| Kind | What it is | Relationship to Pod | Analogy |
| ---- | ---------- | ------------------- | ------- |
| **Pod** | The actual unit of execution | This is the real workload: a Pod runs the process/container | One employee doing the actual work |
| **Deployment** | A controller that creates and manages Pods | Creates and maintains Pods according to the desired replica count | HR/recruiting manager that hires and supervises employees |
| **Job** | A workload controller for finite tasks | Creates Pods to run a batch job until completion | Project manager assigning a one-time task |
| **Namespace** | A logical boundary/folder for objects | Every Pod must live inside a Namespace | A team office or a room in a building |
| **ResourceQuota** | Namespace-level total budget | Limits the combined resource requests of all Pods in the Namespace | A department's office budget cap |
| **LimitRange** | Default/min/max resource policy for a Namespace | Fills in missing resource values for Pods; enforces minimum and maximum constraints | Default equipment rules for new employees |
| **PriorityClass** | A named priority level with an integer value | Pods reference the PriorityClass by name to indicate scheduling priority | VIP tier / priority label |

`--context` selects which Kubernetes cluster/context to use. Once `kind-mle-lab` is the current context, it can be omitted.

<details>
<summary>Scheduler — taints, tolerations &amp; failure reasons</summary>

### Scheduler

The scheduler is the control-plane component that decides which node a Pending Pod runs on:

```text
Job/Deployment creates a Pod → Pod has no node → Scheduler evaluates each node:
    taint without a matching toleration?  → rejected
    insufficient CPU / memory / GPU?       → rejected
    other placement constraints unmet?     → rejected
    all satisfied?                         → assign Pod to node
no node qualifies → Pod stays Pending (FailedScheduling)
```

#### Pod vs Node: what to inspect

```text
POD                              NODE
Demand                           Supply
- CPU / memory / GPU request     - CPU / memory / GPU allocatable
Permission                       Restriction
- tolerations                    - taints
```

```bash
kubectl describe pod <pod>        # why was the Pod not scheduled?
kubectl get pod <pod> -o yaml     # what does the Pod request/tolerate?
kubectl describe node <node>      # what can the Node provide/restrict?
```

#### `taint` and `toleration`

```text
NODE                              POD
🔒 taint                           🔑 toleration
workload=reserved:NoSchedule      workload=reserved:NoSchedule
        └────────── matches ───────────────┘
                    ↓
       This taint does NOT block this Pod
```

A **taint** on a node says "do not schedule Pods onto me unless they tolerate this"; a **toleration** on a Pod says "I am allowed through this specific block". **Taint blocks. Toleration unblocks that specific taint — it removes the barrier, but does not schedule the Pod by itself**; CPU/memory/GPU fit is still checked afterward.

```bash
kubectl taint node <node> workload=reserved:NoSchedule
kubectl describe node <node>
```

#### Resource failure vs taint failure

```bash
0/4 nodes are available:
2 Insufficient nvidia.com/gpu
2 node(s) had untolerated taint {workload: reserved}
```

```text
2 nodes: Pod asks for GPU → not enough schedulable GPU → Insufficient nvidia.com/gpu  (resource-fit failure)
2 nodes: node has workload=reserved taint, Pod lacks toleration → untolerated taint   (scheduling-policy failure)
```

Neither means the training application started and then failed.

</details>

---

## Job Lab

Generate:

```bash
kubectl create job finite-job \
  --image=busybox:1.36 \
  --dry-run=client \
  -o yaml \
  -- sh -c 'echo "job started"; sleep 5; echo "job finished"' \
  > job.yaml
```

Relevant configuration:

```yaml
spec:
  template:
    spec:
      restartPolicy: Never
```

Apply and observe:

```bash
kubectl apply -f job.yaml
kubectl get jobs
kubectl get pods -w
```

Expected: `Running → Completed`. A successful process exits `0`, satisfying the Job's completion requirement.

---

## Deployment Lab

Generate:

```bash
kubectl create deployment finite-deployment \
  --image=busybox:1.36 \
  --dry-run=client \
  -o yaml \
  > deployment.yaml
```

Amend the container:

```yaml
spec:
  replicas: 1
  template:
    spec:
      restartPolicy: Always
      containers:
        - name: busybox
          image: busybox:1.36
          command:
            - sh
            - -c
            - |
              echo "started"
              sleep 5
              echo "finished"
```

Apply and observe:

```bash
kubectl apply -f deployment.yaml
kubectl get deployments
kubectl get pods -w
```

Observed: `Running → Completed → restarted → Running → Completed → CrashLoopBackOff → restarted...`

The process exits successfully, but the Deployment expects a continuously running replica.

---

## Job vs Deployment

```text
Job:         finite process exits 0 → completion satisfied → no restart
Deployment:  finite process exits 0 → restartPolicy: Always → restarted → repeated termination → backoff
```

Use a **Job** for finite workloads such as training/batch processing. Use a **Deployment** for continuously running workloads such as inference services.

---

## Job Running vs Pod Running

**Pod Running** = the process is alive (kubelet's view). **Job Running** = the controller has not yet observed completion (Job controller's view).

```text
Scheduler      Can the Pod be placed?     → Pending / Scheduled
Container      Is the process alive?      → Running / Terminated
Application    Is training progressing?   → epochs / checkpoints
```

Kubernetes only observes the first two layers — it does not understand your training loop.

| Job       | Pod                     | Meaning / next step                                                    |
| --------- | ----------------------- | ---------------------------------------------------------------------- |
| Running   | Running                 | Normal — verify the app is progressing (logs, epochs, GPU utilization) |
| Running   | Pending                 | Scheduling problem — check resources, taints, GPU (see Scheduler)      |
| Running   | Running, logs stuck, GPU idle | Application stuck (deadlock, DDP rank waiting, dataloader, infinite loop) — the process never exits, so the Job stays Running |
| Completed | Completed               | Process exited `0`, completion satisfied                               |

Mental model: Kubernetes may report everything healthy while the ML workload is not making progress. When both Job and Pod are Running, ask layer 3: *is the application actually advancing?*

---

## Service Discovery and DNS Lab

Inference Pods are disposable: their IPs change after a restart or rollout. A **Service** gives clients a stable endpoint; a **Deployment** keeps the desired inference Pods running.

```text
client Pod → http://inference-service:80 → Service (stable ClusterIP)
    → EndpointSlice (current Ready Pod IPs) → inference Pod A / B
```

<details>
<summary>Hands-on: create, test, and debug Service discovery</summary>

Create two backend Pods, expose them, and confirm the Service tracks their IPs:

```bash
kubectl create deployment inference --image=nginx:alpine --replicas=2
kubectl expose deployment inference --name=inference-service --port=80 --target-port=80

kubectl get service inference-service
kubectl get endpointslice -l kubernetes.io/service-name=inference-service
```

`kubectl expose` creates a Service; it does not create Pods. Kubernetes matches the Service selector to ready Pod labels and records their IPs in EndpointSlices.

Test discovery from a temporary client, then delete a backend Pod and confirm the client is unaffected:

```bash
kubectl run client --image=busybox:1.36 --restart=Never --command -- sleep 3600
kubectl exec client -- nslookup inference-service    # resolves to the Service's ClusterIP, not a Pod IP
kubectl exec client -- wget -qO- http://inference-service

kubectl delete pod <inference-pod>                   # Deployment replaces it, likely with a new IP
kubectl get endpointslice -l kubernetes.io/service-name=inference-service
kubectl exec client -- wget -qO- http://inference-service   # unchanged — client never saw the IP change
```

When a Service has no endpoints, trace the request path and inspect selector vs labels:

```text
Client → DNS → Service → selector → EndpointSlice → backend Pods
```

```bash
kubectl get service inference-service -o yaml     # selector
kubectl get pods --show-labels                    # do Pod labels actually match it?
```

</details>

---

## CrashLoopBackOff

`CrashLoopBackOff` does **not necessarily mean the application crashed** — it means the container repeatedly terminates after Kubernetes restarts it, so Kubernetes applies increasing delay before the next attempt.

```bash
kubectl describe pod <pod-name>     # check Last State: Terminated
kubectl logs <pod-name> --previous  # the previous container's logs
```

| Reason | Exit Code | Meaning |
| --- | --- | --- |
| `Completed` | 0 | successful termination |
| `Error` | 1 | application failure |
| `OOMKilled` | 137 | killed for exceeding memory |

```text
CrashLoopBackOff = "container keeps terminating" ≠ "application definitely crashed"
    ↓
kubectl describe pod + kubectl logs --previous → Reason + Exit Code + logs
```

## Resource requests

```bash
kubectl run impossible-request \
  --image=busybox:1.36 \
  --restart=Never \
  --dry-run=client -o yaml > resource-demo.yaml
```

Amend [./resource-demo.yaml](./resource-demo.yaml) with a resources block the node can't satisfy:

```yaml
spec:
  containers:
    - name: impossible-request
      image: busybox:1.36
      command: ["sh", "-c", "sleep 300"]
      resources:
        requests:
          cpu: "6"
          memory: "4Gi"
        limits:
          cpu: "6"
          memory: "4Gi"
```

`requests` are what the scheduler uses for placement; `limits` are what the kubelet enforces at runtime — setting both equal gives guaranteed QoS.

* CPU can be overcommitted: the scheduler places Pods by `requests`, but summed `limits` may exceed node capacity, since CPU is time-shareable.
* GPUs normally cannot: a device plugin advertises discrete resources like `nvidia.com/gpu: 1`, and the scheduler never double-allocates one — requests must equal limits unless time-slicing/MIG/vGPU is explicitly configured.

```bash
kubectl apply -f resource-demo.yaml
kubectl describe pod impossible-request
```

```text
Warning  FailedScheduling  0/1 nodes are available: 1 Insufficient cpu.
```

Use the same [Pod-vs-Node inspection](#pod-vs-node-what-to-inspect) as the Scheduler section above to read it:

```text
Pod Pending → kubectl describe pod → read FailedScheduling reason
   resource problem? → compare Pod requests vs Node allocatable
   taint problem?    → compare Node taint vs Pod toleration
```

---

## Admission Failure vs Scheduling Failure

On a shared ML cluster, each team usually gets its own **Namespace** with a **ResourceQuota** — a check *before* the scheduler, so a workload can be blocked in two different places.

* **Namespace** — a named scope every Pod belongs to. Without `-n`, `kubectl` uses `default`.
* **ResourceQuota** — a budget for one namespace: the **sum** of `requests` across all its Pods (plus object counts) may not exceed `hard`.

```text
Namespace team-a
├── ResourceQuota team-a-quota   (requests.cpu ≤ 1, requests.memory ≤ 1Gi, pods ≤ 4)
├── Pod p1  requests.cpu=600m    ← counted against the quota
└── Pod p2  requests.cpu=600m    ← would make the sum 1200m > 1 → rejected
```

### Lab

```bash
kubectl create namespace team-a
kubectl create quota team-a-quota -n team-a \
  --hard=requests.cpu=1,requests.memory=1Gi,pods=4
kubectl describe quota -n team-a
```

Generate a Pod with requests (once a quota sets `requests.cpu`/`requests.memory`, every Pod in the namespace must declare them, unless a LimitRange fills in defaults):

```bash
kubectl run p1 -n team-a --image=busybox:1.36 --restart=Never \
  --dry-run=client -o yaml -- sleep 600 > quota-pod.yaml
```

```yaml
    resources:
      requests:
        cpu: 600m
        memory: 64Mi
```

Apply `p1`, then the same spec as `p2`:

```bash
kubectl apply -f quota-pod.yaml
sed 's/name: p1/name: p2/' quota-pod.yaml | kubectl apply -f -
kubectl get pods -n team-a
```

### Observed

```text
Error from server (NotFound): error when creating "pod.yaml": namespaces "team-a" not found

Error from server (Forbidden): error when creating "STDIN": pods "p2" is forbidden:
exceeded quota: team-a-quota, requested: requests.cpu=600m,
used: requests.cpu=600m, limited: requests.cpu=1
```

`requested` (600m) + `used` (600m) = 1200m > `limited` (1). `p2` never appears in `kubectl get pods` — the API server refused to create it. Compare with [Resource requests](#resource-requests): `impossible-request` passed admission (no quota in `default`), was created, and stayed `Pending` because no node could fit it.

### Mental model

```text
kubectl apply
    ↓
API server — admission: does the namespace exist? does the quota have room?
    ↓ no  → Forbidden / NotFound — Pod object is NEVER created
    ↓ yes
Pod created (no node yet)
    ↓
Scheduler — placement: requests vs node allocatable, taints/tolerations
    ↓ no  → Pod exists but stays Pending (FailedScheduling)
    ↓ yes
kubelet starts the container → Running
```

| | Admission failure | Scheduling failure |
| --- | --- | --- |
| Question | *Is this team allowed to use it?* (policy) | *Is there physically room?* (capacity) |
| Decided by | API server (ResourceQuota admission) | kube-scheduler |
| Pod object | Never created | Exists, `Pending` |
| Where to look | `kubectl describe job/deployment` → `FailedCreate`, then `kubectl describe quota` | `kubectl describe pod` → `FailedScheduling` |

The two are independent: a team can have quota left while every node is full (Pending), or an empty cluster can still reject a team that spent its budget (Forbidden). When a Job/Deployment creates the Pod, note that a quota rejection lands on the **controller** (`FailedCreate`), not on a Pod — there is no Pod to `describe`.

For an ML platform, this is how GPU budgets per research team are enforced — e.g. `requests.nvidia.com/gpu: "8"` in a team's ResourceQuota caps that team at 8 GPUs regardless of how many are free in the cluster.
