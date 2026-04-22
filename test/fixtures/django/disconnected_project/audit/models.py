from django.db import models


class AuditLog(models.Model):
    payload = models.JSONField(blank=True, null=True)
