# Kubernetes Lab — Job vs Deployment

Local Kubernetes lab using **kind + Podman** on an M2 MacBook.

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

Mental model:

```text
Controller → Pod → Container → Process
```

* **Job/Deployment**: manages desired state.
* **Pod**: Kubernetes execution unit.
* **Container**: runs inside the Pod.
* **Process**: actual application execution.

`--context` selects which Kubernetes cluster/context to use. Once `kind-mle-lab` is the current context, it can be omitted.

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

Expected:

```text
Running → Completed
```

A successful process exits `0`, satisfying the Job's completion requirement.

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

Observed behavior:

```text
Running
→ Completed
→ restarted
→ Running
→ Completed
→ CrashLoopBackOff
→ restarted...
```

The process exits successfully, but the Deployment expects a continuously running replica.

---

## Job vs Deployment

```text
Job
finite process exits 0
→ completion satisfied
→ no restart

Deployment
finite process exits 0
→ restartPolicy: Always
→ container restarted
→ repeated termination
→ restart backoff
```

Use a **Job** for finite workloads such as training/batch processing.

Use a **Deployment** for continuously running workloads such as inference services.

---

## CrashLoopBackOff

`CrashLoopBackOff` does **not necessarily mean the application crashed**.

It means the container repeatedly terminates after Kubernetes restarts it, so Kubernetes applies increasing delay before subsequent restart attempts.

To determine **why the process terminated**:

```bash
kubectl describe pod <pod-name>
```

Check:

```text
Last State:   Terminated
Reason:       Completed
Exit Code:    0
```

Successful termination:

```text
Reason:     Completed
Exit Code:  0
```

Application failure:

```text
Reason:     Error
Exit Code:  1
```

Example OOM termination:

```text
Reason:     OOMKilled
Exit Code:  137
```

Also inspect the previous container's logs:

```bash
kubectl logs <pod-name> --previous
```

Troubleshooting mental model:

```text
CrashLoopBackOff
        ↓
"Container keeps terminating"

        ≠

"Application definitely crashed"
```

Always ask:

```text
Why did the previous process terminate?
        ↓
kubectl describe pod
        +
kubectl logs --previous
        ↓
Reason + Exit Code + logs
```

## Resource requests

```bash
kubectl run impossible-request \
  --image=busybox:1.36 \
  --restart=Never \
  --dry-run=client -o yaml > resource-demo.yaml
```

Then, amend [./resource-demo.yaml](./resource-demo.yaml) with a resources block the node can't satisfy:

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

`requests` are what the scheduler uses to decide placement; `limits` are what the kubelet enforces at runtime. Setting both equal gives guaranteed, QoS-level CPU. Applying the amended manifest will leave the pod `Pending`. Then:

```bash
kubectl apply -f resource-demo.yaml
kubectl get pods
kubectl describe pod impossible-request
```

Then,

```bash
Events:
  Type     Reason            Age   From               Message
  ----     ------            ----  ----               -------
  Warning  FailedScheduling  13s   default-scheduler  0/1 nodes are available: 1 Insufficient cpu. no new claims to deallocate, preemption: 0/1 nodes are available: 1 Preemption is not helpful for scheduling.
```
