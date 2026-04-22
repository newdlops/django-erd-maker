from django.db import models


class Store(models.Model):
    name = models.CharField(max_length=80)


class Receipt(models.Model):
    store = models.ForeignKey("sales.Store", on_delete=models.CASCADE)
    total = models.DecimalField(decimal_places=2, max_digits=10)
