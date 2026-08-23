# Kubernetes Lab

Local Kubernetes practice environment using **kind + Podman** on an M2 MacBook.

## Cluster

Start Podman and the existing kind cluster:

```bash
podman machine start
podman start mle-lab-control-plane

kubectl config use-context kind-mle-lab
kubectl get nodes
```

## Controller vs Execution

In this Kubernetes context:

Controller = manages the desired state.
Execution = the Pod/container actually doing the work.

For your Job:

```bash
Job: finite-job               ← controller-level object
       │
       │ creates/manages
       ▼
Pod: finite-job-jflkr         ← execution-level object
       │
       ▼
busybox container
       │
       ▼
your shell command
```

So:

```bash
kubectl get jobs
```

answers: “Has the requested work completed successfully?”

Whereas:

```bash
kubectl get pods
```

answers: “What is happening to the thing actually executing the workload?”

For example:

```bash
JOB                         POD
finite-job                  finite-job-jflkr
Complete                    Completed
1/1 completion              container exited 0
```

One precision: Pod is the Kubernetes execution unit; the actual OS execution is ultimately the container process(es) inside that Pod.

So a useful hierarchy is:

```bash
Controller → Pod → Container → Process
```

## Job vs Deployment Lab

### Job

Generate a manifest:

```bash
kubectl create job finite-job \
  --image=busybox:1.36 \
  --dry-run=client \
  -o yaml \
  -- sh -c 'echo "job started"; sleep 5; echo "job finished"' \
  > job.yaml
```

Apply and observe:

```bash
kubectl apply -f job.yaml
kubectl get jobs
kubectl get pods -w
```

A successful finite workload reaches `Completed`.

### Deployment

Generate a manifest:

```bash
kubectl create deployment finite-deployment \
  --image=busybox:1.36 \
  --dry-run=client \
  -o yaml \
  > deployment.yaml
```

Amend the container to run the same finite command, then:

```bash
kubectl apply -f deployment.yaml
kubectl get deployments
kubectl get pods -w
```

Observe the container restart after the finite process exits.

## Key Concepts

```text
Job
→ finite work
→ exit 0
→ completion satisfied

Deployment
→ continuously running workload
→ process exits
→ container restarted
```

Useful commands:

```bash
kubectl get jobs       # Job/controller state
kubectl get pods       # Pod/execution state
kubectl get pods -w    # Watch state changes
kubectl describe pod <pod>
kubectl logs <pod>
```

`--context` selects the Kubernetes context. Once `kind-mle-lab` is the current context, it can be omitted.

The compact mental model is:

```bash
--context  → WHERE?
get jobs   → controller state?
get pods   → execution state?
-w         → keep watching changes?
```
